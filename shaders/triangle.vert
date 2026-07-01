#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) out vec3 fragColor;

layout(std430, binding = 0) readonly buffer PositionBuffer {
	vec2 positions[];
};

layout(std430, binding = 1) readonly buffer ColorBuffer {
	float colors[];
};

layout(std430, binding = 2) readonly buffer SortedIndices {
	uint sorted_indices[];
};

void main ()
{
	uint tri = gl_VertexIndex / 3u;
	uint vert = gl_VertexIndex % 3u;
	uint src_tri = sorted_indices[tri];
	uint index = src_tri * 3u + vert;

	gl_Position = vec4 (positions[index], 0.0, 1.0);
	fragColor = vec3 (colors[index * 3u + 0u], colors[index * 3u + 1u], colors[index * 3u + 2u]);
}
