# Benchmark fixtures

`baseline-v1` is a versioned input scene for native benchmark runs. Generate it with
`vkgsplat_fixture_gen`; the normal 3DGS PLY loader reads the result. Generated PLY
files are build artifacts and are not committed to the repository.

## Baseline profile

| Setting | Value |
| --- | --- |
| Profile | `baseline-v1` |
| Distribution | `volume-cloud-v1` |
| Generator version | 1 |
| Gaussian count | 262,144 |
| Seed | `0x5EED2026` |
| Frame | 1920 × 1080, mono 1 × 1 |
| Camera | position (0, 0, 6), target (0, 0, 0), up (0, 1, 0) |
| Projection | 45° vertical FOV, near 0.1, far 100.0 |
| SH | degree 3, 3 DC and 45 rest coefficients |
| Renderer | ImGui off, GPU timers off, validation off, uncapped frame rate |

The C++ source of these values is `benchmark/fixture/profile.hpp`. Future benchmark
consumers should use `BaselineProfile()` and the renderer-facing adapter
`MakeRendererConfig()` rather than copy camera or rendering values. The fixture
generation library remains independent of the renderer. The current projection
path evaluates degree 3; this profile does not add a renderer SH option.

## Generation contract

Every attribute uses a SplitMix64 sample derived from the seed, zero-based Gaussian
index, and attribute index. The integer mixer uses fixed 64-bit wraparound constants.
Positions are sampled throughout x ∈ [-2.75, 2.75), y ∈ [-1.5, 1.5), and z ∈ [-1, 1)
on a 1/4096-unit grid. Scale logits come from {-4.25, -4, -3.75, -3.5}; opacity
logits come from {0, 0.5, 1, 1.5}. Quaternion rotations use eight exact unit-length
entries in `(w, x, y, z)` order. DC and higher-order SH values use signed,
power-of-two quantized values; all 45 rest coefficients are populated. Integer
quantization keeps the emitted float bits stable across supported native builds.

The writer emits binary little-endian PLY 1.0 with 62 float32 properties per record:
`x y z`, zero normals `nx ny nz`, `f_dc_0..2`, `f_rest_0..44`, `opacity`,
`scale_0..2`, and `rot_0..3`. Scales are log-space values and opacity is a logit,
matching the production loader and shaders. Baseline vertex data is 65,011,712 bytes
plus the PLY header.

## Reproduce and verify

```sh
vkgsplat_fixture_gen --profile baseline-v1 --output build/fixtures/baseline-v1.ply
vkgsplat_fixture_gen --verify build/fixtures/baseline-v1.ply
```

Create the output directory first. The generator writes a neighboring
`baseline-v1.ply.json` with the scene hash, file size, versions, camera, and renderer
settings. The checksum is FNV-1a 64-bit over the entire PLY file. It is a
noncryptographic content fingerprint, not a security mechanism. The 128-Gaussian
golden scene (`baseline-v1` seed, count 128) has checksum
`0x6c7d8773eadab651`; its output is labeled `custom-v1` because the count differs.
The full 262,144-Gaussian `baseline-v1` PLY has pinned checksum
`0xad124868b0d228b3` (65,013,243 total bytes). Canonical manifest creation and
verification reject any other checksum, even if the manifest hash is recomputed.

```sh
vkgsplat_fixture_gen --profile baseline-v1 --count 1024 --output build/fixtures/smoke.ply
vkgsplat_fixture_gen --verify build/fixtures/smoke.ply
```

`--count` and `--seed` produce custom fixtures. Their manifests retain the
distribution and generator version but identify the profile as `custom-v1`.
Verification checks the full manifest, file size, and content hash. A matching
checksum identifies the workload input, so benchmark reports should record it.
Verification requires the exact generated JSON representation, including field
order and whitespace; reformatting the manifest invalidates it. Numeric manifest
values are written using the classic locale.

Increase the generator version when sample mapping, tables, or PLY values change.
Introduce a new profile ID when the canonical scene, camera, resolution, or renderer
settings change. Freeze `baseline-v1` after GPU characterization; do not silently
compare measurements from different profile IDs or scene hashes.

## Characterization and limits

The camera faces the center of the bounded volume with depth spanning roughly 5 to
7 world units. The scene is intended to exercise projection, SH evaluation, binning,
and compositing. Projection visibility, radii, covered area, depth distribution, and
non-background output still need measurement on the supported Vulkan path before
the profile is considered visually characterized. The opt-in
`benchmark.fixture.gpu.smoke` test generates the full fixture, waits for a completed
headless frame, decodes its PNG, and requires at least 1,000 pixels to differ from
the black clear color; it is enabled with
`vkgsplat_ENABLE_HEADLESS_GPU_TESTS` on a capable device.

This synthetic cloud favors reproducibility over the spatial and appearance
statistics of a reconstructed scene. Fixed input and camera do not imply identical
GPU pixels or timings across drivers and hardware.
