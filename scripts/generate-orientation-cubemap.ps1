[CmdletBinding()]
param(
    [string] $OutputPath = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $repositoryRoot `
        'content\source\sky\shark_orientation_sky_srgb.dds'
}

$resolvedOutput = [IO.Path]::GetFullPath($OutputPath)
$resolvedRoot = [IO.Path]::GetFullPath($repositoryRoot).TrimEnd(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar
)
$rootPrefix = $resolvedRoot + [IO.Path]::DirectorySeparatorChar
if (-not $resolvedOutput.StartsWith(
        $rootPrefix,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "The generated cubemap must remain inside the Shark repository."
}
if ([IO.Path]::GetExtension($resolvedOutput) -ne '.dds') {
    throw "The generated cubemap path must use the .dds extension."
}

$parentDirectory = Split-Path -Parent $resolvedOutput
[IO.Directory]::CreateDirectory($parentDirectory) | Out-Null

$size = 8
$pitch = $size * 4
$faceCount = 6
$faceTags = @(32, 64, 96, 128, 160, 192)

$ddsMagic = 0x20534444
$ddsHeaderSize = 124
$ddsPixelFormatSize = 32
$ddsFlags = 0x0000100F
$ddsPixelFormatFourCc = 0x00000004
$fourCcDx10 = 0x30315844
$ddsCapsTextureComplex = 0x00001008
$ddsCaps2AllCubeFaces = 0x0000FE00
$dxgiFormatRgba8Srgb = 29
$d3d10ResourceDimensionTexture2D = 3
$d3d11ResourceMiscTextureCube = 0x4
$ddsAlphaModeOpaque = 3

$stream = [IO.File]::Open(
    $resolvedOutput,
    [IO.FileMode]::Create,
    [IO.FileAccess]::Write,
    [IO.FileShare]::None
)
$writer = [IO.BinaryWriter]::new($stream)

try {
    $writer.Write([uint32] $ddsMagic)
    $writer.Write([uint32] $ddsHeaderSize)
    $writer.Write([uint32] $ddsFlags)
    $writer.Write([uint32] $size)
    $writer.Write([uint32] $size)
    $writer.Write([uint32] $pitch)
    $writer.Write([uint32] 0)
    $writer.Write([uint32] 1)
    for ($index = 0; $index -lt 11; ++$index) {
        $writer.Write([uint32] 0)
    }

    $writer.Write([uint32] $ddsPixelFormatSize)
    $writer.Write([uint32] $ddsPixelFormatFourCc)
    $writer.Write([uint32] $fourCcDx10)
    for ($index = 0; $index -lt 5; ++$index) {
        $writer.Write([uint32] 0)
    }

    $writer.Write([uint32] $ddsCapsTextureComplex)
    $writer.Write([uint32] $ddsCaps2AllCubeFaces)
    $writer.Write([uint32] 0)
    $writer.Write([uint32] 0)
    $writer.Write([uint32] 0)

    $writer.Write([uint32] $dxgiFormatRgba8Srgb)
    $writer.Write([uint32] $d3d10ResourceDimensionTexture2D)
    $writer.Write([uint32] $d3d11ResourceMiscTextureCube)
    $writer.Write([uint32] 1)
    $writer.Write([uint32] $ddsAlphaModeOpaque)

    for ($face = 0; $face -lt $faceCount; ++$face) {
        for ($y = 0; $y -lt $size; ++$y) {
            for ($x = 0; $x -lt $size; ++$x) {
                $writer.Write([byte] $faceTags[$face])
                $writer.Write([byte] (16 + $x * 24))
                $writer.Write([byte] (16 + $y * 24))
                $writer.Write([byte] 255)
            }
        }
    }
}
finally {
    $writer.Dispose()
    $stream.Dispose()
}

$hash = Get-FileHash -LiteralPath $resolvedOutput -Algorithm SHA256
Write-Output ('Generated {0} ({1} bytes, SHA-256 {2})' -f `
    $resolvedOutput,
    (Get-Item -LiteralPath $resolvedOutput).Length,
    $hash.Hash.ToLowerInvariant()
)
