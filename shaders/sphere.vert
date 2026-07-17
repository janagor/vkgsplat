#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragLocal;

layout(push_constant) uniform CameraMatrices {
	mat4 view;
	mat4 proj;
} camera;

layout(std430, binding = 0) readonly buffer PositionBuffer {
	vec3 positions[];
};

layout(std430, binding = 1) readonly buffer ColorBuffer {
	float colors[];
};

layout(std430, binding = 2) readonly buffer SortedIndices {
	uint sorted_indices[];
};

const float SPHERE_RADIUS = 0.22;
const vec2 QUAD_VERTS[6] = vec2[](
	vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),
	vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0)
);

void main()
{
	uint display_slot = gl_InstanceIndex;
	uint src_sphere = sorted_indices[display_slot];
	vec3 center = positions[src_sphere];
	vec2 local = QUAD_VERTS[gl_VertexIndex] * SPHERE_RADIUS;

	vec3 world_pos = center + vec3(local, 0.0);
	gl_Position = camera.proj * camera.view * vec4(world_pos, 1.0);
	fragColor = vec3(colors[src_sphere]);
	fragLocal = QUAD_VERTS[gl_VertexIndex];
}
