# Camera paths

The native headless renderer accepts a version 1 JSON camera path. Each keyframe supplies a time in seconds, a position, a vertical field of view in degrees, and either a look-at target or an orientation quaternion. Quaternions use `[x, y, z, w]`; local `-Z` points forward and local `+Y` points up.

```json
{
  "version": 1,
  "keyframes": [
    { "time": 0, "position": [0, 1.5, 10], "target": [0, 0, 0], "fov_degrees": 45 },
    { "time": 2, "position": [4, 2, 8], "target": [0, 0, 0], "fov_degrees": 40 },
    { "time": 4, "position": [0, 1.5, 10], "target": [0, 0, 0], "fov_degrees": 45 }
  ]
}
```

An optional `up` vector accompanies `target`. For explicit rotation, replace `target` and `up` with `"orientation": [x, y, z, w]`. The first time must be zero, and times must increase strictly. At least two keyframes are required.

Render PNG frames:

```sh
vkgsplat_headless --ply scene.ply --camera-path camera.json --target offscreen --sequence-dir frames --fps 60
```

Record video with the existing encoder:

```sh
vkgsplat_headless --ply scene.ply --camera-path camera.json --target offscreen --output animation.mp4 --fps 60
```

For a single PNG, `--camera-path` uses the first pose. Animated output samples frame `i` at `i × fps-denominator / fps` seconds. The default frame count is the first frame index whose sampled time reaches the path duration, so the endpoint keyframe is excluded from a fixed-duration sequence. `--frames` overrides this count; frames beyond the duration hold the final pose. Without a camera path, the existing static screenshot and orbital recording behavior applies.

Position uses time-aware cubic Hermite interpolation for three or more keys and linear interpolation for two keys. Orientation uses shortest-path quaternion interpolation, and field of view is linear. Cubic position can overshoot between keys.
