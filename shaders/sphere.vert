#version 460
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : enable

// Oriented screen-space Gaussian quads (PlayCanvas / SuperSplat style).
layout(location = 0) flat out vec3 fragColor;
layout(location = 1) out vec2 fragCorner;// unit-disk UV in [-1, 1]
layout(location = 2) flat out float fragOpacity;

layout(push_constant) uniform RasterPush {
	vec4 camera_position;
	uvec2 viewport;
	uint tile_size;
	uint tiles_x;
	vec4 background;
	uint sh_degree;
	uint _pad0;
	uint _pad1;
	uint _pad2;
} push;

const uint HEAP_PROJECTED = 4u;
const uint HEAP_SORTED_VALUES = 8u;

const uint PROJECTED_STRIDE = 11u;
const uint PROJ_MEAN = 0u;
const uint PROJ_COV = 2u;
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
	float cov_xx = projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_COV];
	float cov_xy = projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_COV + 1u];
	float cov_yy = projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_COV + 2u];
	float radius = projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_RADIUS];
	vec3 color = vec3(
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_COLOR],
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_COLOR + 1u],
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_COLOR + 2u]);
	float opacity = projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_OPACITY];

	if (radius < 1.0) {
		gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
		fragColor = vec3(0.0);
		fragCorner = vec2(0.0);
		fragOpacity = 0.0;
		return;
	}

	float mid = 0.5 * (cov_xx + cov_yy);
	float radius_eig = length(vec2((cov_xx - cov_yy) * 0.5, cov_xy));
	float lambda1 = mid + radius_eig;
	float lambda2 = max(mid - radius_eig, 0.1);

	// PlayCanvas gsplatCorner: extent = 2 * sqrt(λ) on each eigenaxis.
	float l1 = 2.0 * sqrt(lambda1);
	float l2 = 2.0 * sqrt(lambda2);

	// Axis-aligned Σ has cov_xy=0 and λ1=cov_xx → (0,0); normalize(0,0) is NaN.
	vec2 diagonal_vector = vec2(cov_xy, lambda1 - cov_xx);
	float diag_len2 = dot(diagonal_vector, diagonal_vector);
	diagonal_vector = (diag_len2 > 1e-12) ? (diagonal_vector * inversesqrt(diag_len2)) : vec2(1.0, 0.0);

	vec2 v1 = l1 * diagonal_vector;
	vec2 v2 = l2 * vec2(diagonal_vector.y, -diagonal_vector.x);

	vec2 local = QUAD_VERTS[gl_VertexIndex];
	vec2 offset = local.x * v1 + local.y * v2;
	vec2 pixel = mean + offset;

	float width = max(float(push.viewport.x), 1.0);
	float height = max(float(push.viewport.y), 1.0);
	vec2 ndc = vec2(pixel.x / width, pixel.y / height) * 2.0 - 1.0;
	gl_Position = vec4(ndc, 0.0, 1.0);

	fragColor = color;
	fragCorner = local;
	fragOpacity = opacity;
}
