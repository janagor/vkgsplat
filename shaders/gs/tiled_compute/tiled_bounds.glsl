#ifndef TILED_BOUNDS_GLSL
#define TILED_BOUNDS_GLSL

// Half-open tile rectangle. Invalid or invisible splats return an empty rectangle.
uvec4 tiled_bounds(vec2 mean, float radius, float depth, uvec2 viewport, uint tile_size, uvec2 grid)
{
  if (radius < 1.0 || isnan(radius) || isinf(radius) || isnan(depth) || isinf(depth)
      || depth <= 0.0 || any(isnan(mean)) || any(isinf(mean))) {
    return uvec4(0u);
  }
  vec2 minimum = mean - vec2(radius);
  vec2 maximum = mean + vec2(radius);
  if (maximum.x <= 0.0 || maximum.y <= 0.0
      || minimum.x >= float(viewport.x) || minimum.y >= float(viewport.y)) {
    return uvec4(0u);
  }
  vec2 tile_minimum = floor(clamp(minimum, vec2(0.0), vec2(viewport)) / float(tile_size));
  vec2 tile_maximum = ceil(clamp(maximum, vec2(0.0), vec2(viewport)) / float(tile_size));
  uvec2 first = min(uvec2(tile_minimum), grid);
  uvec2 last = min(uvec2(tile_maximum), grid);
  return uvec4(first, max(first, last));
}

#endif
