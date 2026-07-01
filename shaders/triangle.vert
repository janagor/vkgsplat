#version 450
#extension GL_ARB_separate_shader_objects : enable

layout (location = 0) out vec3 fragColor;

layout(std430, binding = 0) readonly buffer PositionBuffer {
	vec2 positions[];
};

layout(std430, binding = 1) readonly buffer ColorBuffer {
	float colors[];
};

void main ()
{
	uint index = gl_VertexIndex;
	gl_Position = vec4 (positions[index], 0.0, 1.0);
	fragColor = vec3 (colors[index * 3 + 0], colors[index * 3 + 1], colors[index * 3 + 2]);
}