# Rendering fixture configurations

Run these commands from the repository root after obtaining the exact
`resources/scene.ply` used for this baseline:

```sh
sha256sum --check test/fixtures/rendering/scene.sha256
out/build/unixlike-clang-release/src/vkgsplat_app/vkgsplat_app \
  --config test/fixtures/rendering/mono.toml
out/build/unixlike-clang-release/src/vkgsplat_app/vkgsplat_app \
  --config test/fixtures/rendering/quilt_2x2.toml
```

Press F2 to save a scene-only PNG. The app defaults to a 1024 x 1024 window;
the 2 x 2 atlas is 2048 x 2048. The PNG output location is chosen by the app,
so move each image to `test/reference/mono.png` or
`test/reference/quilt_2x2.png` after capture and record its SHA-256.

The scene is machine-local and is not checked into this repository. No external
download URL or redistribution terms were verified. The initial camera values
are copied from `config/example.toml`; they have not been visually checked for
this scene, so the camera and any resulting images are provisional. Capture and
review on a compatible Vulkan device before treating images as references.
The current environment could not enumerate a physical Vulkan device or open
an XCB surface, so no reference images are included yet.
