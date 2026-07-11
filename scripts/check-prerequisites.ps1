#Requires -Version 5.1

[CmdletBinding()]
param()

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

# F-001 owns these machine-tool requirements. Project-restored package versions
# are documented below but deliberately are not searched for on PATH.
$MinimumWindowsBuild = 22000
$MinimumGitCore = [version]'2.55.0'
$MinimumGitWindowsRevision = 2
$MinimumVisualStudio = [version]'18.0'
$MaximumVisualStudio = [version]'19.0'
$RequiredMsvcFamily = '14.50'
$MinimumCMake = [version]'4.2.0'
$MinimumWindowsSdk = [version]'10.0.26100.0'
$MinimumPix = [version]'2603.25'

$script:Results = @()

function Add-CheckResult {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('PASS', 'FAIL', 'WARN', 'INFO')]
        [string]$Status,

        [Parameter(Mandatory = $true)]
        [string]$Phase,

        [Parameter(Mandatory = $true)]
        [string]$Name,

        [Parameter(Mandatory = $true)]
        [string]$Detail,

        [string]$Remediation = '',

        [bool]$BlocksF002 = $false
    )

    $result = [pscustomobject]@{
        Status      = $Status
        Phase       = $Phase
        Name        = $Name
        Detail      = $Detail
        Remediation = $Remediation
        BlocksF002  = $BlocksF002
    }
    $script:Results += $result

    $color = switch ($Status) {
        'PASS' { 'Green' }
        'FAIL' { 'Red' }
        'WARN' { 'Yellow' }
        default { 'Cyan' }
    }

    Write-Host ('[{0}]' -f $Status) -ForegroundColor $color -NoNewline
    Write-Host (' {0,-8} {1}' -f $Phase, $Name)
    Write-Host ('       {0}' -f $Detail)
    if (-not [string]::IsNullOrWhiteSpace($Remediation)) {
        Write-Host ('       Action: {0}' -f $Remediation)
    }
}

function Get-ApplicationPath {
    param([Parameter(Mandatory = $true)][string]$Name)

    $command = Get-Command -Name $Name -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -eq $command) {
        return $null
    }

    return $command.Source
}

function Get-ProgramFilesRoots {
    $roots = @()

    # A 32-bit PowerShell process redirects ProgramFiles to Program Files (x86).
    # ProgramW6432 retains the native 64-bit location on a 64-bit OS.
    if ([Environment]::Is64BitOperatingSystem -and
        -not [string]::IsNullOrWhiteSpace($env:ProgramW6432)) {
        $roots += $env:ProgramW6432
    }

    foreach ($folderName in @('ProgramFiles', 'ProgramFilesX86')) {
        $folder = [Environment]::GetFolderPath($folderName)
        if (-not [string]::IsNullOrWhiteSpace($folder)) {
            $roots += $folder
        }
    }

    return $roots | Select-Object -Unique
}

function Get-NumericVersion {
    param([AllowNull()][string]$Text)

    if ([string]::IsNullOrWhiteSpace($Text)) {
        return $null
    }

    $match = [regex]::Match($Text, '(?<!\d)(\d+\.\d+(?:\.\d+){0,2})')
    if (-not $match.Success) {
        return $null
    }

    try {
        return [version]$match.Groups[1].Value
    }
    catch {
        return $null
    }
}

function Invoke-VersionProbe {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [string[]]$Arguments = @('--version')
    )

    try {
        $output = & $Path @Arguments 2>&1
        $exitCode = $LASTEXITCODE
        return [pscustomobject]@{
            Succeeded = ($exitCode -eq 0)
            ExitCode  = $exitCode
            Text      = (($output | Out-String).Trim())
        }
    }
    catch {
        return [pscustomobject]@{
            Succeeded = $false
            ExitCode  = -1
            Text      = $_.Exception.Message
        }
    }
}

function Get-GitForWindowsVersion {
    param([AllowNull()][string]$Text)

    if ([string]::IsNullOrWhiteSpace($Text)) {
        return $null
    }

    $match = [regex]::Match($Text, '(\d+\.\d+\.\d+)\.windows\.(\d+)')
    if (-not $match.Success) {
        return $null
    }

    try {
        return [pscustomobject]@{
            CoreVersion    = [version]$match.Groups[1].Value
            WindowsRevision = [int]$match.Groups[2].Value
        }
    }
    catch {
        return $null
    }
}

function Get-VsWherePath {
    $candidates = @()
    $programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        $candidates += (Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe')
    }

    $commandPath = Get-ApplicationPath -Name 'vswhere.exe'
    if (-not [string]::IsNullOrWhiteSpace($commandPath)) {
        $candidates += $commandPath
    }

    return $candidates |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
        Select-Object -Unique -First 1
}

function Get-VisualStudioInstallations {
    param([Parameter(Mandatory = $true)][string]$VsWherePath)

    $arguments = @(
        '-version', ('[{0},{1})' -f $MinimumVisualStudio, $MaximumVisualStudio),
        '-products', '*',
        '-format', 'json',
        '-utf8'
    )

    $jsonLines = & $VsWherePath @arguments 2>$null
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0 -or $null -eq $jsonLines) {
        return $null
    }

    $json = ($jsonLines -join [Environment]::NewLine).Trim().TrimStart([char]0xFEFF)
    if ([string]::IsNullOrWhiteSpace($json) -or $json -eq '[]') {
        return $null
    }

    try {
        $parsed = $json | ConvertFrom-Json
        if ($null -eq $parsed) {
            return $null
        }
        $instances = @($parsed) | Where-Object { $null -ne $_ }
        return $instances
    }
    catch {
        return $null
    }
}

function Get-VisualStudioDiagnostic {
    param([Parameter(Mandatory = $true)][string]$VsWherePath)

    try {
        $jsonLines = & $VsWherePath -all -prerelease -products '*' -format json -utf8 2>$null
        if ($LASTEXITCODE -ne 0 -or $null -eq $jsonLines) {
            return 'No usable Visual Studio installation was reported.'
        }

        $json = ($jsonLines -join [Environment]::NewLine).Trim()
        if ([string]::IsNullOrWhiteSpace($json) -or $json -eq '[]') {
            return 'No Visual Studio installation was found.'
        }

        $instances = @($json | ConvertFrom-Json)
        $summaries = @()
        foreach ($instance in $instances) {
            $name = if ($null -ne $instance.displayName) { [string]$instance.displayName } else { 'Visual Studio' }
            $version = if ($null -ne $instance.installationVersion) { [string]$instance.installationVersion } else { 'unknown version' }
            $state = if ($instance.isComplete -and $instance.isLaunchable) { 'complete' } else { 'incomplete or not launchable' }
            $summaries += ('{0} {1} ({2})' -f $name, $version, $state)
        }
        return ($summaries -join '; ')
    }
    catch {
        return ('Visual Studio diagnostic query failed: {0}' -f $_.Exception.Message)
    }
}

function Get-RequiredMsvcToolset {
    param([Parameter(Mandatory = $true)][string]$InstallationPath)

    $toolsetsRoot = Join-Path $InstallationPath 'VC\Tools\MSVC'
    if (-not (Test-Path -LiteralPath $toolsetsRoot -PathType Container)) {
        return $null
    }

    $candidates = @()
    foreach ($directory in (Get-ChildItem -LiteralPath $toolsetsRoot -Directory -ErrorAction SilentlyContinue)) {
        if ($directory.Name -notlike ($RequiredMsvcFamily + '.*')) {
            continue
        }

        $clPath = Join-Path $directory.FullName 'bin\Hostx64\x64\cl.exe'
        $vectorPath = Join-Path $directory.FullName 'include\vector'
        $runtimeLibraryPath = Join-Path $directory.FullName 'lib\x64\libcpmt.lib'
        $complete = (Test-Path -LiteralPath $clPath -PathType Leaf) -and
            (Test-Path -LiteralPath $vectorPath -PathType Leaf) -and
            (Test-Path -LiteralPath $runtimeLibraryPath -PathType Leaf)

        $parsedVersion = Get-NumericVersion -Text $directory.Name
        if ($null -ne $parsedVersion) {
            $candidates += [pscustomobject]@{
                Version  = $parsedVersion
                Path     = $directory.FullName
                ClPath   = $clPath
                Complete = $complete
            }
        }
    }

    return $candidates |
        Where-Object { $_.Complete } |
        Sort-Object -Property Version -Descending |
        Select-Object -First 1
}

function Get-CMakeCandidates {
    param([AllowNull()][string]$VisualStudioPath)

    $candidates = @()
    $pathCandidate = Get-ApplicationPath -Name 'cmake.exe'
    if (-not [string]::IsNullOrWhiteSpace($pathCandidate)) {
        $candidates += $pathCandidate
    }

    if (-not [string]::IsNullOrWhiteSpace($VisualStudioPath)) {
        $candidates += (Join-Path $VisualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
    }

    foreach ($programFilesRoot in @(Get-ProgramFilesRoots)) {
        $candidates += (Join-Path $programFilesRoot 'CMake\bin\cmake.exe')
    }

    return $candidates |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
        Select-Object -Unique
}

function Get-BestCMake {
    param([AllowNull()][string]$VisualStudioPath)

    $probes = @()
    foreach ($candidate in @(Get-CMakeCandidates -VisualStudioPath $VisualStudioPath)) {
        $probe = Invoke-VersionProbe -Path $candidate
        $version = Get-NumericVersion -Text $probe.Text
        if ($probe.Succeeded -and $null -ne $version) {
            $probes += [pscustomobject]@{
                Version = $version
                Path    = $candidate
                Text    = $probe.Text
            }
        }
    }

    return $probes |
        Sort-Object -Property Version -Descending |
        Select-Object -First 1
}

function Get-WindowsSdkRoots {
    $roots = @()

    try {
        $baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
            [Microsoft.Win32.RegistryHive]::LocalMachine,
            [Microsoft.Win32.RegistryView]::Registry64
        )
        $subKey = $baseKey.OpenSubKey('SOFTWARE\Microsoft\Windows Kits\Installed Roots')
        if ($null -ne $subKey) {
            $registryRoot = [string]$subKey.GetValue('KitsRoot10', '')
            if (-not [string]::IsNullOrWhiteSpace($registryRoot)) {
                $roots += $registryRoot
            }
            $subKey.Dispose()
        }
        $baseKey.Dispose()
    }
    catch {
        # Conventional-path fallbacks below remain valid and read-only.
    }

    foreach ($programFilesRoot in @(Get-ProgramFilesRoots)) {
        $roots += (Join-Path $programFilesRoot 'Windows Kits\10')
    }

    return $roots |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_ -PathType Container) } |
        Select-Object -Unique
}

function Get-WindowsSdkInventory {
    $inventory = @()

    foreach ($root in @(Get-WindowsSdkRoots)) {
        $includeRoot = Join-Path $root 'Include'
        if (-not (Test-Path -LiteralPath $includeRoot -PathType Container)) {
            continue
        }

        foreach ($directory in (Get-ChildItem -LiteralPath $includeRoot -Directory -ErrorAction SilentlyContinue)) {
            try {
                $version = [version]$directory.Name
            }
            catch {
                continue
            }

            $requiredRelativePaths = @(
                'Include\{0}\um\Windows.h',
                'Include\{0}\um\d3d12.h',
                'Include\{0}\um\d3d12sdklayers.h',
                'Include\{0}\shared\dxgi1_6.h',
                'Include\{0}\ucrt\stdlib.h',
                'Lib\{0}\um\x64\d3d12.lib',
                'Lib\{0}\um\x64\dxgi.lib',
                'Lib\{0}\um\x64\dxguid.lib',
                'Lib\{0}\um\x64\kernel32.lib',
                'Lib\{0}\ucrt\x64\ucrt.lib'
            )

            $missing = @()
            foreach ($relativeTemplate in $requiredRelativePaths) {
                $relativePath = $relativeTemplate -f $directory.Name
                $fullPath = Join-Path $root $relativePath
                if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
                    $missing += $relativePath
                }
            }

            $inventory += [pscustomobject]@{
                Version  = $version
                Root     = $root
                Complete = ($missing.Count -eq 0)
                Missing  = $missing
            }
        }
    }

    return $inventory | Sort-Object -Property Version -Descending
}

function Get-NinjaCandidates {
    param([AllowNull()][string]$VisualStudioPath)

    $candidates = @()
    $pathCandidate = Get-ApplicationPath -Name 'ninja.exe'
    if (-not [string]::IsNullOrWhiteSpace($pathCandidate)) {
        $candidates += $pathCandidate
    }
    if (-not [string]::IsNullOrWhiteSpace($VisualStudioPath)) {
        $candidates += (Join-Path $VisualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe')
    }

    return $candidates |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
        Select-Object -Unique
}

function Get-PixInstallations {
    $candidates = @()
    foreach ($commandName in @('PIX.exe', 'PIXWin.exe')) {
        $pathPix = Get-ApplicationPath -Name $commandName
        if (-not [string]::IsNullOrWhiteSpace($pathPix)) {
            $candidates += $pathPix
        }
    }

    foreach ($programFilesRoot in @(Get-ProgramFilesRoots)) {
        $pixRoot = Join-Path $programFilesRoot 'Microsoft PIX'
        $candidates += (Join-Path $pixRoot 'PIX.exe')
        if (Test-Path -LiteralPath $pixRoot -PathType Container) {
            $versionDirectories = Get-ChildItem -LiteralPath $pixRoot -Directory -ErrorAction SilentlyContinue
            foreach ($directory in $versionDirectories) {
                $candidates += (Join-Path $directory.FullName 'PIX.exe')
                $candidates += (Join-Path $directory.FullName 'PIXWin.exe')
            }
        }
    }

    $installations = @()
    $paths = $candidates |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
        Select-Object -Unique
    foreach ($path in $paths) {
        $item = Get-Item -LiteralPath $path
        $versionText = [string]$item.VersionInfo.ProductVersion
        if ([string]::IsNullOrWhiteSpace($versionText)) {
            $versionText = [string]$item.VersionInfo.FileVersion
        }

        $combinedText = $path + ' ' + $versionText
        $versionMatch = [regex]::Match($combinedText, '(?<!\d)(\d{4}\.\d{1,3})(?!\d)')
        $parsedVersion = $null
        if ($versionMatch.Success) {
            try {
                $parsedVersion = [version]$versionMatch.Groups[1].Value
            }
            catch {
                $parsedVersion = $null
            }
        }

        $installations += [pscustomobject]@{
            Path        = $path
            Version     = $parsedVersion
            VersionText = $versionText
            IsPreview   = ($combinedText -match '(?i)preview')
        }
    }

    return $installations
}

function Get-NativeSystemDirectory {
    if ([Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess) {
        return (Join-Path $env:WINDIR 'Sysnative')
    }
    return (Join-Path $env:WINDIR 'System32')
}

try {
    Write-Host 'Shark Windows prerequisite check (F-001)'
    Write-Host 'Read-only: no installs, downloads, file writes, registry writes, or PATH changes.'
    Write-Host ''

    # Windows version. The build number is authoritative because Windows 11 can
    # still expose an older ProductName through compatibility registry values.
    $buildNumber = 0
    $ubr = 0
    $displayVersion = ''
    $installationType = ''
    $isClientOperatingSystem = $false
    try {
        $baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
            [Microsoft.Win32.RegistryHive]::LocalMachine,
            [Microsoft.Win32.RegistryView]::Registry64
        )
        $currentVersionKey = $baseKey.OpenSubKey('SOFTWARE\Microsoft\Windows NT\CurrentVersion')
        if ($null -ne $currentVersionKey) {
            $buildText = [string]$currentVersionKey.GetValue('CurrentBuildNumber', '0')
            if ([string]::IsNullOrWhiteSpace($buildText) -or $buildText -eq '0') {
                $buildText = [string]$currentVersionKey.GetValue('CurrentBuild', '0')
            }
            [void][int]::TryParse($buildText, [ref]$buildNumber)
            $ubrValue = $currentVersionKey.GetValue('UBR', 0)
            if ($null -ne $ubrValue) {
                $ubr = [int]$ubrValue
            }
            $displayVersion = [string]$currentVersionKey.GetValue('DisplayVersion', '')
            $installationType = [string]$currentVersionKey.GetValue('InstallationType', '')
            $currentVersionKey.Dispose()
        }
        $baseKey.Dispose()
    }
    catch {
        $os = Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
        [void][int]::TryParse([string]$os.BuildNumber, [ref]$buildNumber)
        $isClientOperatingSystem = ([int]$os.ProductType -eq 1)
        $installationType = if ($isClientOperatingSystem) { 'Client' } else { 'Server' }
    }

    if (-not [string]::IsNullOrWhiteSpace($installationType)) {
        $isClientOperatingSystem = ($installationType -eq 'Client')
    }
    else {
        try {
            $os = Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
            $isClientOperatingSystem = ([int]$os.ProductType -eq 1)
            $installationType = if ($isClientOperatingSystem) { 'Client' } else { 'Server' }
        }
        catch {
            $installationType = 'unknown'
        }
    }

    if ($buildNumber -ge $MinimumWindowsBuild -and $isClientOperatingSystem) {
        Add-CheckResult -Status PASS -Phase 'F-002' -Name 'Windows 11' -BlocksF002 $true `
            -Detail ('Build {0}.{1} {2}; installation type {3}' -f $buildNumber, $ubr, $displayVersion, $installationType).Trim()
    }
    else {
        Add-CheckResult -Status FAIL -Phase 'F-002' -Name 'Windows 11' -BlocksF002 $true `
            -Detail ('Detected build {0}, installation type {1}; Shark requires Windows 11 Client build {2} or newer.' -f $buildNumber, $installationType, $MinimumWindowsBuild) `
            -Remediation 'Install a supported, fully updated x64 release of Windows 11.'
    }

    $nativeArchitecture = if (-not [string]::IsNullOrWhiteSpace($env:PROCESSOR_ARCHITEW6432)) {
        $env:PROCESSOR_ARCHITEW6432
    }
    else {
        $env:PROCESSOR_ARCHITECTURE
    }

    if ([Environment]::Is64BitOperatingSystem -and $nativeArchitecture -eq 'AMD64') {
        $processNote = if ([Environment]::Is64BitProcess) { '64-bit PowerShell process' } else { '32-bit PowerShell process on x64 Windows' }
        Add-CheckResult -Status PASS -Phase 'F-002' -Name 'x64 architecture' -BlocksF002 $true `
            -Detail ('AMD64 operating system; {0}.' -f $processNote)
    }
    else {
        Add-CheckResult -Status FAIL -Phase 'F-002' -Name 'x64 architecture' -BlocksF002 $true `
            -Detail ('Native architecture is {0}; Is64BitOperatingSystem={1}.' -f $nativeArchitecture, [Environment]::Is64BitOperatingSystem) `
            -Remediation 'Use an x64 (AMD64) Windows 11 development machine.'
    }

    Add-CheckResult -Status PASS -Phase 'F-002' -Name 'PowerShell' -BlocksF002 $true `
        -Detail ('Windows PowerShell {0} ({1})' -f $PSVersionTable.PSVersion, $PSVersionTable.PSEdition)

    $gitPath = Get-ApplicationPath -Name 'git.exe'
    if ([string]::IsNullOrWhiteSpace($gitPath)) {
        Add-CheckResult -Status FAIL -Phase 'F-002' -Name 'Git for Windows' -BlocksF002 $true `
            -Detail 'git.exe was not found on PATH.' `
            -Remediation 'Install current Git for Windows, then open a new PowerShell window.'
    }
    else {
        $gitProbe = Invoke-VersionProbe -Path $gitPath
        $gitVersion = Get-GitForWindowsVersion -Text $gitProbe.Text
        $gitVersionIsCurrent = $false
        if ($null -ne $gitVersion) {
            $gitVersionIsCurrent = ($gitVersion.CoreVersion -gt $MinimumGitCore) -or
                (($gitVersion.CoreVersion -eq $MinimumGitCore) -and
                    ($gitVersion.WindowsRevision -ge $MinimumGitWindowsRevision))
        }

        if ($gitProbe.Succeeded -and $gitVersionIsCurrent) {
            Add-CheckResult -Status PASS -Phase 'F-002' -Name 'Git for Windows' -BlocksF002 $true `
                -Detail ('{0}; {1}' -f (($gitProbe.Text -split "`r?`n")[0]), $gitPath)
        }
        else {
            Add-CheckResult -Status FAIL -Phase 'F-002' -Name 'Git for Windows' -BlocksF002 $true `
                -Detail ('Detected {0}; Shark requires maintained Git for Windows {1}.windows.{2} or newer.' -f (($gitProbe.Text -split "`r?`n")[0]), $MinimumGitCore, $MinimumGitWindowsRevision) `
                -Remediation 'Upgrade Git for Windows before F-002 dependency restoration (winget upgrade --id Git.Git --exact).'
        }
    }

    $visualStudioPath = $null
    $vsWherePath = Get-VsWherePath
    if ([string]::IsNullOrWhiteSpace($vsWherePath)) {
        Add-CheckResult -Status FAIL -Phase 'F-002' -Name 'Visual Studio 2026 / MSVC 14.50 LTS' -BlocksF002 $true `
            -Detail 'vswhere.exe was not found, so a complete Visual Studio installation cannot be verified.' `
            -Remediation 'Install Visual Studio 2026 Community or Build Tools with Desktop development with C++.'
    }
    else {
        $vsInstallations = @(
            Get-VisualStudioInstallations -VsWherePath $vsWherePath |
                Where-Object { $null -ne $_ -and $null -ne $_.installationPath }
        )
        if ($vsInstallations.Count -eq 0) {
            $diagnostic = Get-VisualStudioDiagnostic -VsWherePath $vsWherePath
            Add-CheckResult -Status FAIL -Phase 'F-002' -Name 'Visual Studio 2026 / MSVC 14.50 LTS' -BlocksF002 $true `
                -Detail $diagnostic `
                -Remediation 'In Visual Studio Installer, install the stable Visual Studio 2026 Desktop development with C++ workload and the MSVC 14.50 LTS x64/x86 component.'
        }
        else {
            $selectedVs = $null
            $selectedMsvc = $null
            $sortedInstallations = $vsInstallations |
                Sort-Object -Property { [version]$_.installationVersion } -Descending
            foreach ($candidateVs in $sortedInstallations) {
                $candidatePath = [string]$candidateVs.installationPath
                $candidateMsvc = Get-RequiredMsvcToolset -InstallationPath $candidatePath
                if ($null -ne $candidateMsvc) {
                    $selectedVs = $candidateVs
                    $selectedMsvc = $candidateMsvc
                    break
                }
            }

            if ($null -ne $selectedVs) {
                $visualStudioPath = [string]$selectedVs.installationPath
                $vsVersion = [version]$selectedVs.installationVersion
                Add-CheckResult -Status PASS -Phase 'F-002' -Name 'Visual Studio 2026 / MSVC 14.50 LTS' -BlocksF002 $true `
                    -Detail ('{0} {1}; MSVC {2}; {3}' -f $selectedVs.displayName, $vsVersion, $selectedMsvc.Version, $selectedMsvc.ClPath)
            }
            else {
                $installationDetails = ($sortedInstallations | ForEach-Object {
                    '{0} {1} at {2}' -f $_.displayName, $_.installationVersion, $_.installationPath
                }) -join '; '
                Add-CheckResult -Status FAIL -Phase 'F-002' -Name 'Visual Studio 2026 / MSVC 14.50 LTS' -BlocksF002 $true `
                    -Detail ('Stable Visual Studio 2026 installation(s) found, but none contains a complete MSVC 14.50 LTS x64 payload: {0}.' -f $installationDetails) `
                    -Remediation 'Modify Visual Studio 2026: keep Desktop development with C++, then explicitly select MSVC 14.50 LTS x64/x86 build tools.'
            }
        }
    }

    $cmake = Get-BestCMake -VisualStudioPath $visualStudioPath
    if ($null -eq $cmake) {
        Add-CheckResult -Status FAIL -Phase 'F-002' -Name 'CMake' -BlocksF002 $true `
            -Detail ('No working CMake {0} or newer was found on PATH or in Visual Studio.' -f $MinimumCMake) `
            -Remediation 'Install current stable CMake (4.4 recommended) or the Visual Studio C++ CMake tools component.'
    }
    elseif ($cmake.Version -lt $MinimumCMake) {
        Add-CheckResult -Status FAIL -Phase 'F-002' -Name 'CMake' -BlocksF002 $true `
            -Detail ('CMake {0} at {1}; version {2} or newer is required for the Visual Studio 18 2026 generator.' -f $cmake.Version, $cmake.Path, $MinimumCMake) `
            -Remediation 'Upgrade to current stable CMake (4.4 recommended).'
    }
    else {
        Add-CheckResult -Status PASS -Phase 'F-002' -Name 'CMake' -BlocksF002 $true `
            -Detail ('CMake {0}; {1}' -f $cmake.Version, $cmake.Path)
    }

    $sdkInventory = @(Get-WindowsSdkInventory)
    $usableSdk = $sdkInventory |
        Where-Object { $_.Complete -and $_.Version -ge $MinimumWindowsSdk } |
        Sort-Object -Property Version -Descending |
        Select-Object -First 1
    if ($null -ne $usableSdk) {
        Add-CheckResult -Status PASS -Phase 'F-002' -Name 'Windows 11 SDK' -BlocksF002 $true `
            -Detail ('SDK {0}; headers and x64 D3D12/UCRT libraries verified under {1}' -f $usableSdk.Version, $usableSdk.Root)
    }
    else {
        $detectedVersions = if ($sdkInventory.Count -gt 0) {
            (($sdkInventory | ForEach-Object { '{0} ({1})' -f $_.Version, $(if ($_.Complete) { 'complete' } else { 'incomplete' }) }) -join ', ')
        }
        else {
            'none'
        }
        Add-CheckResult -Status FAIL -Phase 'F-002' -Name 'Windows 11 SDK' -BlocksF002 $true `
            -Detail ('No complete SDK {0} or newer was found. Detected SDK payloads: {1}.' -f $MinimumWindowsSdk, $detectedVersions) `
            -Remediation 'Modify the Visual Studio C++ workload and install a supported Windows 11 SDK (10.0.26100 family or newer).'
    }

    $systemDirectory = Get-NativeSystemDirectory
    $d3d12Runtime = Join-Path $systemDirectory 'd3d12.dll'
    if (Test-Path -LiteralPath $d3d12Runtime -PathType Leaf) {
        Add-CheckResult -Status PASS -Phase 'F-002' -Name 'Direct3D 12 runtime' -BlocksF002 $true `
            -Detail $d3d12Runtime
    }
    else {
        Add-CheckResult -Status FAIL -Phase 'F-002' -Name 'Direct3D 12 runtime' -BlocksF002 $true `
            -Detail ('{0} was not found.' -f $d3d12Runtime) `
            -Remediation 'Repair or update Windows 11.'
    }

    $debugLayerPath = Join-Path $systemDirectory 'd3d12SDKLayers.dll'
    $graphicsCapabilityState = 'query unavailable'
    try {
        $capability = Get-WindowsCapability -Online -Name 'Tools.Graphics.DirectX~~~~0.0.1.0' -ErrorAction Stop |
            Select-Object -First 1
        if ($null -ne $capability) {
            $graphicsCapabilityState = [string]$capability.State
        }
    }
    catch {
        $graphicsCapabilityState = 'query unavailable without elevation or capability provider'
    }

    if (Test-Path -LiteralPath $debugLayerPath -PathType Leaf) {
        Add-CheckResult -Status PASS -Phase 'G-001' -Name 'Windows Graphics Tools' `
            -Detail ('D3D12 debug layer found at {0}; capability state: {1}.' -f $debugLayerPath, $graphicsCapabilityState)
    }
    else {
        Add-CheckResult -Status WARN -Phase 'G-001' -Name 'Windows Graphics Tools' `
            -Detail ('D3D12 debug layer is missing; capability state: {0}.' -f $graphicsCapabilityState) `
            -Remediation 'Before G-001, install the Windows optional feature Graphics Tools (Tools.Graphics.DirectX~~~~0.0.1.0).'
    }

    $pixInstallations = @(Get-PixInstallations)
    $usablePix = $pixInstallations |
        Where-Object { -not $_.IsPreview -and $null -ne $_.Version -and $_.Version -ge $MinimumPix } |
        Sort-Object -Property Version -Descending |
        Select-Object -First 1
    if ($null -eq $usablePix) {
        $pixDetail = if ($pixInstallations.Count -eq 0) {
            'PIX was not found in its standard installation locations.'
        }
        else {
            $detectedPix = ($pixInstallations | ForEach-Object {
                $kind = if ($_.IsPreview) { 'preview' } else { 'main/unknown' }
                '{0} ({1}, {2})' -f $_.Path, $_.VersionText, $kind
            }) -join '; '
            'No non-preview PIX {0} or newer was found. Detected: {1}' -f $MinimumPix, $detectedPix
        }
        Add-CheckResult -Status WARN -Phase 'G-007' -Name 'PIX on Windows' `
            -Detail $pixDetail `
            -Remediation 'Before G-007, install the current non-preview PIX release (winget install Microsoft.PIX).'
    }
    else {
        Add-CheckResult -Status PASS -Phase 'G-007' -Name 'PIX on Windows' `
            -Detail ('PIX {0}; {1}' -f $usablePix.Version, $usablePix.Path)
    }

    $ninjaCandidates = @(Get-NinjaCandidates -VisualStudioPath $visualStudioPath)
    if ($ninjaCandidates.Count -eq 0) {
        Add-CheckResult -Status WARN -Phase 'Optional' -Name 'Ninja' `
            -Detail 'Ninja was not found; it is not required because Shark will use the Visual Studio 18 2026 generator.' `
            -Remediation 'Install Ninja only if a later measured workflow benefits from it.'
    }
    else {
        $ninjaPath = $ninjaCandidates[0]
        $ninjaProbe = Invoke-VersionProbe -Path $ninjaPath
        if ($ninjaProbe.Succeeded) {
            Add-CheckResult -Status PASS -Phase 'Optional' -Name 'Ninja' `
                -Detail ('{0}; {1}' -f (($ninjaProbe.Text -split "`r?`n")[0]), $ninjaPath)
        }
        else {
            Add-CheckResult -Status WARN -Phase 'Optional' -Name 'Ninja' `
                -Detail ('A Ninja executable exists but could not run: {0}' -f $ninjaProbe.Text) `
                -Remediation 'Repair Ninja only if a later measured workflow requires it.'
        }
    }

    try {
        $gpus = @(Get-CimInstance -ClassName Win32_VideoController -ErrorAction Stop |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_.Name) })
        if ($gpus.Count -gt 0) {
            $gpuDetails = ($gpus | ForEach-Object {
                '{0} (driver {1}, status {2})' -f $_.Name, $_.DriverVersion, $_.Status
            }) -join '; '
            Add-CheckResult -Status PASS -Phase 'Hardware' -Name 'Display adapters' `
                -Detail ($gpuDetails + '. Feature Level and Shader Model support cannot be proven by WMI; G-001 will query D3D12 capabilities.')
        }
        else {
            Add-CheckResult -Status WARN -Phase 'Hardware' -Name 'Display adapters' `
                -Detail 'No display adapter was returned by Win32_VideoController.' `
                -Remediation 'Verify graphics drivers; G-001 will perform the authoritative D3D12 capability query.'
        }
    }
    catch {
        Add-CheckResult -Status WARN -Phase 'Hardware' -Name 'Display adapters' `
            -Detail ('GPU inventory failed: {0}' -f $_.Exception.Message) `
            -Remediation 'Verify graphics drivers; G-001 will perform the authoritative D3D12 capability query.'
    }

    $globalDxc = Get-ApplicationPath -Name 'dxc.exe'
    $globalDxcDetail = if ([string]::IsNullOrWhiteSpace($globalDxc)) {
        'No global dxc.exe found, which is expected.'
    }
    else {
        'A global dxc.exe exists at {0}, but Shark will not depend on it.' -f $globalDxc
    }
    Add-CheckResult -Status INFO -Phase 'F-002' -Name 'Project-restored DirectX tools' `
        -Detail ($globalDxcDetail + ' F-002 will select and pin retail Agility SDK, DXC, WARP, and WinPixEventRuntime packages inside the project.')

    Write-Host ''
    $passCount = @($script:Results | Where-Object { $_.Status -eq 'PASS' }).Count
    $failCount = @($script:Results | Where-Object { $_.Status -eq 'FAIL' }).Count
    $warnCount = @($script:Results | Where-Object { $_.Status -eq 'WARN' }).Count
    $infoCount = @($script:Results | Where-Object { $_.Status -eq 'INFO' }).Count
    $blockingFailures = @($script:Results | Where-Object { $_.Status -eq 'FAIL' -and $_.BlocksF002 }).Count

    Write-Host ('Summary: {0} PASS, {1} FAIL, {2} WARN, {3} INFO.' -f $passCount, $failCount, $warnCount, $infoCount)
    if ($blockingFailures -gt 0) {
        Write-Host ('F-002 gate: NOT READY ({0} blocking failure(s)).' -f $blockingFailures) -ForegroundColor Red
        exit 1
    }

    Write-Host 'F-002 gate: READY. Later-phase warnings may remain.' -ForegroundColor Green
    exit 0
}
catch {
    Write-Host ''
    Write-Host '[FAIL] Checker internal error' -ForegroundColor Red
    Write-Host ('       {0}' -f $_.Exception.Message)
    Write-Host '       This indicates a checker bug, not a failed machine prerequisite.'
    exit 2
}
