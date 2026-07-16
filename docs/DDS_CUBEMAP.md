# DDS Cubemap Asset and Upload Contract

- **Completed through:** `S-001`
- **Last verified:** July 16, 2026

S-001 establishes Shark's first file-backed texture asset. It loads one
project-owned DDS cubemap into CPU-owned engine records, preserves explicit
linear/sRGB semantics, uploads every face and mip through the existing startup
submission, and creates a persistent texture-cube SRV. The current
`TexturedCube` pass still draws only the procedural checker; S-002 will render
the cubemap as a skybox.

## Tracked orientation fixture

The repository owns
`content/source/sky/shark_orientation_sky_srgb.dds` and its deterministic
PowerShell generator. The fixture is original procedural content dedicated
under CC0-1.0; its directory records the complete provenance and license.

Its fixed contract is:

```text
container       DDS with required DX10 extension header
resource        one Texture2D cubemap
face order      +X, -X, +Y, -Y, +Z, -Z
extent          8x8 per face
mips            1
format          DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
alpha           opaque
file size       1684 bytes
SHA-256         1f37644dd2dcbbb5102fa5ade97dc8f92bbc64769be7648434661eea70c445b1
```

The red channel identifies the face with values `32, 64, 96, 128, 160, 192`.
Green increases from left to right and blue from top to bottom. CPU tests can
therefore detect a face reorder, row flip, or column flip without rendering.

Regenerate it from the repository root with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    .\scripts\generate-orientation-cubemap.ps1
```

The script writes DDS directly; there is no image conversion or third-party
source material. Git treats `.dds` files as binary.

## CPU asset boundary

`shark::assets::DdsCubemap` is move-only and owns all decoded pixel bytes.
Its public header contains only engine enums, dimensions, pitches,
`std::span<const std::byte>` views, paths, and `core::Result`; DirectXTex,
DXGI, D3D12, COM, and scratch-image types remain private to the implementation.

The loader accepts memory or a file path plus an explicit expected color space.
It uses the pinned DirectXTex package for DDS metadata and pixel decoding, then
copies each decoded image into engine-owned storage. Subresources are exposed
in D3D-compatible face-major/mip-minor order:

```text
+X mip 0..N, -X mip 0..N, +Y mip 0..N,
-Y mip 0..N, +Z mip 0..N, -Z mip 0..N
```

S-001 deliberately supports only explicit
`R8G8B8A8_UNORM` and `R8G8B8A8_UNORM_SRGB`. It performs no gamma conversion
and never forces one interpretation onto the other.

Before publishing an asset, the loader requires:

- a bounded, nonempty input with an exact DX10 DDS payload and no trailing
  bytes;
- Texture2D metadata, the texture-cube flag, all six legacy face-cap bits, one
  cube only, square nonzero faces, and depth one;
- at least one mip and no more levels than the mathematical chain permits;
- exact expected face/mip payload size and exactly `6 * mipLevels` decoded
  images;
- matching preflight/decoded metadata, dimensions, format, row pitch, slice
  pitch, and non-null pixels; and
- agreement between the file's exact format and the caller's expected linear
  or sRGB color space.

Missing files and invalid, unsupported, truncated, ambiguous, or internally
inconsistent assets return structured `assets` errors. Legacy DDS pixel
formats and typeless formats are rejected because their color-space intent is
not explicit enough for this boundary.

## Application-local composition

The build copies the tracked DDS to:

```text
<SharkSandbox directory>/content/sky/shark_orientation_sky_srgb.dds
```

The sandbox resolves that path from its executable module directory, not from
the process working directory or source tree. It loads the asset before
`Presentation::create`, converts its metadata to generic
`TextureCubeUploadView` records, and lends those views only for synchronous
startup resource creation. Presentation retains no caller-owned CPU pointer.

DirectXTex is a shared pinned dependency. vcpkg places `DirectXTex.dll` beside
both supported executables; Debug and Release verification checks that the DLL
and DDS are present. A future packaged distribution must also carry
DirectXTex's MIT notice and the supported MSVC/OpenMP runtime; S-001 produces
development outputs only.

## D3D12 resource and descriptor contract

Presentation validates the generic upload view again before using D3D12. It
requires one square six-face RGBA8 cube, a valid partial or complete mip chain,
exact face-major subresource count, and coherent dimensions and pitches. The
selected adapter must report both texture-cube and shader-sampling support for
the exact format.

Startup then:

1. creates one committed default-heap Texture2D array with six slices, the
   source mip count, and the exact source linear/sRGB DXGI format;
2. asks D3D12 for every placed upload footprint;
3. copies every source row into one temporary cubemap upload buffer;
4. records one `CopyTextureRegion` per face/mip in the existing
   `StaticCubeUpload` command list;
5. transitions all cubemap subresources from `COPY_DEST` to
   `PIXEL_SHADER_RESOURCE`;
6. expands the persistent shader-visible texture heap from one to two slots,
   preserving the checker at slot 0; and
7. creates one `D3D12_SRV_DIMENSION_TEXTURECUBE` SRV at slot 1.

The root signature still exposes one SRV table entry and the current pass still
binds heap slot 0. Slot 1 is persistent startup state reserved for S-002; no
new frame-graph resource, pass, barrier, timestamp, draw, or normal-frame wait
is introduced.

## Verification

CPU tests prove the tracked metadata, all six face tags, horizontal/vertical
orientation, face/mip indexing, explicit linear and sRGB handling, partial and
complete mip chains, missing-file errors, and rejection of malformed
dimensions, faces, arrays, formats, color spaces, payload sizes, and mip data.

The hardware, packaged-WARP, and packaged-WARP GPU-validation presentation
smokes retain all G-007 frame invariants and additionally require:

```text
cubemap_texture_creations == 1
cubemap_srv_creations == 1
cubemap_faces_uploaded == 6
cubemap_mip_levels == 1
cubemap_subresources_uploaded == 6
cubemap_source_bytes_uploaded == 1536
persistent_texture_descriptors == 2
texture_srv_creations == 2
cubemap_srgb_resources == 1
```

The startup path remains exactly one static submission, one `StaticCubeUpload`
PIX event, and one bounded initialization wait. The normal frame graph remains
one `TexturedCube` pass with two back-buffer transitions and four timestamps
per submitted frame.

## Explicit non-goals

S-001 does not draw the skybox, modify shaders or the root signature, import
the cubemap into the frame graph, generate mips, convert HDR images, build
irradiance/specular maps, stream textures, compress content, load general 2D
materials, create asset IDs or caches, or establish a general descriptor
allocator. Those capabilities enter only through later focused increments.
