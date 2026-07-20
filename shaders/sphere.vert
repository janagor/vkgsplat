#version 460
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragLocal;
layout(location = 2) out float fragOpacity;

layout(push_constant) uniform CameraMatrices {
	mat4 view;
	mat4 proj;
} camera;

// Heap slot indices must match HeapSlot in descriptor_heap.hpp.
const uint HEAP_GEOMETRY = 0u;
const uint HEAP_APPEARANCE = 1u;
const uint HEAP_SORTED_INDICES = 2u;

// Flat packing must match GaussianGeometry / GaussianAppearance in gaussian_splat.hpp.
// Descriptor-heap struct member access hits Mesa/glslang Offset bugs; use scalar arrays.
const uint GEOMETRY_STRIDE = 11u;
const uint GEOM_POS = 0u;
const uint GEOM_SCALE = 3u;
const uint GEOM_OPACITY = 10u;

const uint APPEARANCE_STRIDE = 48u;
const uint APP_F_DC = 0u;

layout(descriptor_heap, std430) readonly buffer GeometryBuffer {
	float data[];
} geometry_buffers[];

layout(descriptor_heap, std430) readonly buffer AppearanceBuffer {
	float data[];
} appearance_buffers[];

layout(descriptor_heap, std430) readonly buffer SortedIndices {
	uint sorted_indices[];
} sorted_index_buffers[];

const float SH_C0 = 0.28209479177387814;
const vec2 QUAD_VERTS[6] = vec2[](
	vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),
	vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0)
);

float sigmoid(float x)
{
	return 1.0 / (1.0 + exp(-x));
}

vec3 sh0_to_rgb(vec3 f_dc)
{
	return clamp(vec3(0.5) + SH_C0 * f_dc, 0.0, 1.0);
}

vec3 scales_from_log(vec3 log_scale)
{
	return exp(log_scale);
}

void main()
{
	uint display_slot = gl_InstanceIndex;
	uint src_sphere = sorted_index_buffers[HEAP_SORTED_INDICES].sorted_indices[display_slot];

	uint geom_base = src_sphere * GEOMETRY_STRIDE;
	uint app_base = src_sphere * APPEARANCE_STRIDE;

	vec3 center = vec3(
		geometry_buffers[HEAP_GEOMETRY].data[geom_base + GEOM_POS],
		geometry_buffers[HEAP_GEOMETRY].data[geom_base + GEOM_POS + 1u],
		geometry_buffers[HEAP_GEOMETRY].data[geom_base + GEOM_POS + 2u]);
	vec3 scale = scales_from_log(vec3(
		geometry_buffers[HEAP_GEOMETRY].data[geom_base + GEOM_SCALE],
		geometry_buffers[HEAP_GEOMETRY].data[geom_base + GEOM_SCALE + 1u],
		geometry_buffers[HEAP_GEOMETRY].data[geom_base + GEOM_SCALE + 2u]));
	float radius = max(max(scale.x, scale.y), scale.z);
	float opacity = sigmoid(geometry_buffers[HEAP_GEOMETRY].data[geom_base + GEOM_OPACITY]);

	vec2 local = QUAD_VERTS[gl_VertexIndex] * radius;
	vec3 world_pos = center + vec3(local, 0.0);
	gl_Position = camera.proj * camera.view * vec4(world_pos, 1.0);

	vec3 f_dc = vec3(
		appearance_buffers[HEAP_APPEARANCE].data[app_base + APP_F_DC],
		appearance_buffers[HEAP_APPEARANCE].data[app_base + APP_F_DC + 1u],
		appearance_buffers[HEAP_APPEARANCE].data[app_base + APP_F_DC + 2u]);
	fragColor = sh0_to_rgb(f_dc);
	fragOpacity = opacity;
	fragLocal = QUAD_VERTS[gl_VertexIndex];
}
