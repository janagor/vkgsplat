#version 460
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragLocal;

layout(push_constant) uniform CameraMatrices {
	mat4 view;
	mat4 proj;
} camera;

// Heap slot indices must match HeapSlot in descriptor_heap.hpp.
const uint HEAP_POSITION = 0u;
const uint HEAP_COLOR = 1u;
const uint HEAP_SORTED_INDICES = 2u;

layout(descriptor_heap, std430) readonly buffer PositionBuffer {
	float positions[];
} position_buffers[];

layout(descriptor_heap, std430) readonly buffer ColorBuffer {
	float colors[];
} color_buffers[];

layout(descriptor_heap, std430) readonly buffer SortedIndices {
	uint sorted_indices[];
} sorted_index_buffers[];

const float SPHERE_RADIUS = 0.22;
const vec2 QUAD_VERTS[6] = vec2[](
	vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),
	vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0)
);

void main()
{
	uint display_slot = gl_InstanceIndex;
	uint src_sphere = sorted_index_buffers[HEAP_SORTED_INDICES].sorted_indices[display_slot];
	uint pos_base = src_sphere * 3u;
	uint color_base = src_sphere * 3u;
	vec3 center = vec3(
		position_buffers[HEAP_POSITION].positions[pos_base],
		position_buffers[HEAP_POSITION].positions[pos_base + 1u],
		position_buffers[HEAP_POSITION].positions[pos_base + 2u]);
	vec2 local = QUAD_VERTS[gl_VertexIndex] * SPHERE_RADIUS;

	vec3 world_pos = center + vec3(local, 0.0);
	gl_Position = camera.proj * camera.view * vec4(world_pos, 1.0);
	fragColor = vec3(
		color_buffers[HEAP_COLOR].colors[color_base],
		color_buffers[HEAP_COLOR].colors[color_base + 1u],
		color_buffers[HEAP_COLOR].colors[color_base + 2u]);
	fragLocal = QUAD_VERTS[gl_VertexIndex];
}
