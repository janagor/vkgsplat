#version 460
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : enable

// Oriented screen-space quads; fragment shader evaluates the EWA Gaussian per pixel.
layout(location = 0) flat out vec3 fragColor;
layout(location = 1) flat out vec2 fragMean;
layout(location = 2) flat out vec3 fragConic;
layout(location = 3) flat out float fragOpacity;

layout(push_constant) uniform RasterPush {
	vec4 camera_position;
	uvec2 viewport;
	uint tile_size;
	uint tiles_x;
	vec4 background;
	uint sh_degree;
	uint pad0_;
	uvec2 tile_offset;
} push;

const uint HEAP_PROJECTED = 4u;
const uint HEAP_SORTED_VALUES = 8u;

const uint PROJECTED_STRIDE = 11u;
const uint PROJ_MEAN = 0u;
const uint PROJ_CONIC = 2u;
const uint PROJ_RADIUS = 6u;
const uint PROJ_COLOR = 7u;
const uint PROJ_OPACITY = 10u;

layout(descriptor_heap, std430) readonly buffer ProjectedBuffer {
	float data[];
} projected_buffers[];

layout(descriptor_heap, std430) readonly buffer SortedValuesBuffer {
	uint data[];
} sorted_values_buffers[];

const vec2 QUAD_VERTS[6] = vec2[](
	vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),
	vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0)
);

void main()
{
	uint gaussian_id = sorted_values_buffers[HEAP_SORTED_VALUES].data[gl_InstanceIndex];
	uint proj_base = gaussian_id * PROJECTED_STRIDE;

	vec2 mean = vec2(
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_MEAN],
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_MEAN + 1u]);
	vec3 conic = vec3(
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_CONIC],
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_CONIC + 1u],
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_CONIC + 2u]);
	float radius = projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_RADIUS];
	vec3 color = vec3(
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_COLOR],
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_COLOR + 1u],
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_COLOR + 2u]);
	float opacity = projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_OPACITY];

	if (radius < 1.0) {
		gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
		fragColor = vec3(0.0);
		fragMean = vec2(0.0);
		fragConic = vec3(0.0);
		fragOpacity = 0.0;
		return;
	}

	// Invert Σ⁻¹ back to Σ for oriented quad axes.
	float det_c = conic.x * conic.z - conic.y * conic.y;
	if (det_c <= 1e-10) {
		gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
		fragColor = vec3(0.0);
		fragMean = vec2(0.0);
		fragConic = vec3(0.0);
		fragOpacity = 0.0;
		return;
	}
	float inv = 1.0 / det_c;
	float cov_xx = conic.z * inv;
	float cov_xy = -conic.y * inv;
	float cov_yy = conic.x * inv;

	float mid = 0.5 * (cov_xx + cov_yy);
	float radius_eig = length(vec2((cov_xx - cov_yy) * 0.5, cov_xy));
	float lambda1 = mid + radius_eig;
	float lambda2 = max(mid - radius_eig, 0.1);

	float width = max(float(push.viewport.x), 1.0);
	float height = max(float(push.viewport.y), 1.0);
	// PlayCanvas gsplatCorner: extent = 2 * sqrt(2λ), capped vs viewport.
	float vmin = min(1024.0, min(width, height));
	float l1 = 2.0 * min(sqrt(2.0 * lambda1), vmin);
	float l2 = 2.0 * min(sqrt(2.0 * lambda2), vmin);

	vec2 diagonal_vector = vec2(cov_xy, lambda1 - cov_xx);
	float diag_len2 = dot(diagonal_vector, diagonal_vector);
	diagonal_vector = (diag_len2 > 1e-12) ? (diagonal_vector * inversesqrt(diag_len2)) : vec2(1.0, 0.0);

	vec2 v1 = l1 * diagonal_vector;
	vec2 v2 = l2 * vec2(diagonal_vector.y, -diagonal_vector.x);

	vec2 local = QUAD_VERTS[gl_VertexIndex];
	vec2 offset = local.x * v1 + local.y * v2;
	vec2 pixel = mean + offset;
	vec2 ndc = vec2(pixel.x / width, pixel.y / height) * 2.0 - 1.0;
	gl_Position = vec4(ndc, 0.0, 1.0);

	fragColor = color;
	fragMean = mean + vec2(push.tile_offset);
	fragConic = conic;
	fragOpacity = opacity;
}
