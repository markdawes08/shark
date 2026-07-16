# Shark orientation cubemap

`shark_orientation_sky_srgb.dds` is Shark's first file-backed texture asset.
It is a deliberately simple orientation fixture, not a photographed sky.

## Provenance and regeneration

- Source: original procedural content generated inside this repository
- License: CC0-1.0; see [LICENSE.md](LICENSE.md)
- Generator: `scripts/generate-orientation-cubemap.ps1`
- Conversion: none; the generator writes the DDS container directly
- Container: DDS with the DX10 extension header
- Resource: one Texture2D cubemap containing exactly six faces
- Face order: `+X`, `-X`, `+Y`, `-Y`, `+Z`, `-Z`
- Extent: `8x8` texels per face
- Mips: one
- Format: `DXGI_FORMAT_R8G8B8A8_UNORM_SRGB`
- Alpha mode: opaque
- File size: `1684` bytes
- SHA-256:
  `1f37644dd2dcbbb5102fa5ade97dc8f92bbc64769be7648434661eea70c445b1`

Every texel is RGBA8:

```text
R = face tag: 32, 64, 96, 128, 160, or 192
G = 16 + x * 24
B = 16 + y * 24
A = 255
```

The red channel identifies the face, green increases left-to-right, and blue
increases top-to-bottom. This makes face order and row orientation
deterministic before S-002 renders the cubemap.

Regenerate from the repository root with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File `
    .\scripts\generate-orientation-cubemap.ps1
```
