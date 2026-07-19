# DDS Cubemap Asset and Upload Contract

- **Completed through:** `T-005`
- **Last verified:** July 19, 2026

S-001 establishes Shark's first file-backed texture asset. It loads one
project-owned DDS cubemap into CPU-owned engine records, preserves explicit
linear/sRGB semantics, uploads every face and mip through the existing startup
submission, and creates a persistent texture-cube SRV. S-002 now declares that
persistent resource as an exact pixel-shader read and renders it through the
named `Skybox` pass. S-002A replaces that diagnostic image's visible use with
a procedural daylight sky. The loader, app-local fixture, startup upload, GPU
resource, and descriptor remain as the bounded S-001 proof, but the normal
frame graph no longer imports, reads, or binds the cubemap.
S-003 adds a separate project-generated linear-HDR environment path; it does
not reinterpret or repurpose this sRGB orientation fixture.

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
`shark::renderer::Renderer::create`, converts its metadata to generic
`TextureCubeUploadView` records, and lends those views only for synchronous
startup resource creation. `Renderer` retains no caller-owned CPU pointer.

DirectXTex is a shared pinned dependency. vcpkg places `DirectXTex.dll` beside
both supported executables; Debug and Release verification checks that the DLL
and DDS are present. A future packaged distribution must also carry
DirectXTex's MIT notice and the supported MSVC/OpenMP runtime; S-001 produces
development outputs only.

## D3D12 resource and descriptor contract

The private D3D12 renderer backend validates the generic upload view again
before using D3D12. It
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
   `StaticSceneUpload` command list;
5. transitions all cubemap subresources from `COPY_DEST` to
   `PIXEL_SHADER_RESOURCE`;
6. reserves cubemap slot 1 in the persistent shader-visible texture heap,
   preserving the checker at slot 0; and
7. creates one `D3D12_SRV_DIMENSION_TEXTURECUBE` SRV at slot 1.

The root signature still exposes one SRV table entry. `TexturedCube` points it
at checker slot 0. S-002A's procedural `Skybox` does not point the table at
slot 1 and does not sample a `TextureCube`; its color comes from the normalized
world direction and the frame's daylight constants. The frame graph therefore
imports only the checker as a persistent pixel-shader read. The cubemap remains
in `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE` after startup until orderly
shutdown, without a normal-frame transition, declaration, descriptor-table
bind, or draw dependency.

## Verification

CPU tests prove the tracked metadata, all six face tags, horizontal/vertical
orientation, face/mip indexing, explicit linear and sRGB handling, partial and
complete mip chains, missing-file errors, and rejection of malformed
dimensions, faces, arrays, formats, color spaces, payload sizes, and mip data.

Every hardware, packaged-WARP, and focused packaged-WARP GPU-validation
presentation submission retains the G-007 per-frame invariants and additionally
requires:

```text
cubemap_texture_creations == 1
cubemap_srv_creations == 1
cubemap_faces_uploaded == 6
cubemap_mip_levels == 1
cubemap_subresources_uploaded == 6
cubemap_source_bytes_uploaded == 1536
persistent_texture_descriptors == 10
cubemap_srgb_resources == 1
```

S-003 raises the complete fixed heap to ten descriptors by adding four
environment/IBL SRVs and one resize-owned HDR scene SRV. The cubemap-specific
creation, face, mip, byte, and sRGB counters remain unchanged; total SRV
creation also includes scene-color recreation after resize.

The startup path remains exactly one static submission, one
`StaticSceneUpload` PIX event, and one bounded initialization wait. The normal
frame graph now has 15 imports, four ordered
`Terrain`/`TexturedCube`/`Skybox`/`ToneMap` passes, three dependencies, six
transitions, 31 elisions, four texture-table binds, and ten timestamps per
frame. With `V` visible terrain chunks it contains `2V + 4` indexed draws plus
the tone-map draw. Each visible terrain draw selects its 384-index LOD0 or
240-index coarse range. T-005's combined 9,984 surface indices, chunk bounds,
query marker, and S-003 material sphere remain packed into the existing terrain
buffers, preserving four total geometry buffers. The DDS-backed cube/sky draw
and upload counts do not vary with terrain visibility or LOD.
Cubemap creation/upload counters remain startup invariants, but there is no
per-frame cubemap read or binding to count.
Hardware and normal WARP execute 1,000 successful presents; focused WARP with
GPU-based validation executes 120 presents, retaining resize and rotation while
intentionally skipping the normal paths' minimize/restore interval, with a
180-second internal deadline and 240-second CTest timeout.

## Explicit non-goals

S-003 does not delete or generalize the S-001 loader, generate mips for this
DDS, reinterpret its color space, stream textures, compress content, load
general 2D materials, create asset IDs/caches, or establish a general
descriptor allocator. The retained orientation resource is not the reflection
environment, image-based light, or hidden color input to either sky mode.
See [the skybox contract](SKYBOX.md) for the current HDR environment and
procedural fallback paths.
T-002 adds canonical terrain queries without changing this retained asset
proof. REN-001 moves the public upload view into `RendererConfig` and the
upload implementation into `engine/renderer/src/d3d12`; the D3D12 RHI no
longer exposes a public `Presentation` class. T-003 adds three separate
terrain arrays and descriptors without repurposing or sampling the retained
cubemap. S-003 was completed on July 18, 2026 and defines its separate
project-generated environment-lighting asset contract without treating this
orientation fixture as production content. T-005 was completed on July 19,
2026 without changing the DDS contract. The next increment is `R-001`, seeded,
bounded GPU rain driven by adjustable precipitation rate and wind.
