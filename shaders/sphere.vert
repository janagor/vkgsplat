#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragLocal;

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

mat4 look_at(vec3 eye, vec3 center, vec3 up)
{
	vec3 f = normalize(center - eye);
	vec3 s = normalize(cross(f, up));
	vec3 u = cross(s, f);
	return mat4(
		vec4(s, 0.0),
		vec4(u, 0.0),
		vec4(-f, 0.0),
		vec4(-dot(s, eye), -dot(u, eye), dot(f, eye), 1.0)
	);
}

mat4 projection_matrix()
{
	float fov = radians(50.0);
	float aspect = 16.0 / 9.0;
	float near_plane = 0.1;
	float far_plane = 100.0;
	float tan_half_fov = tan(fov * 0.5);
	mat4 proj = mat4(
		vec4(1.0 / (aspect * tan_half_fov), 0.0, 0.0, 0.0),
		vec4(0.0, -1.0 / tan_half_fov, 0.0, 0.0),
		vec4(0.0, 0.0, far_plane / (near_plane - far_plane), -1.0),
		vec4(0.0, 0.0, (near_plane * far_plane) / (near_plane - far_plane), 0.0)
	);
	return proj;
}

void main()
{
	uint display_slot = gl_InstanceIndex;
	uint src_sphere = sorted_indices[display_slot];
	vec3 center = positions[display_slot];
	vec2 local = QUAD_VERTS[gl_VertexIndex] * SPHERE_RADIUS;

	vec3 world_pos = center + vec3(local, 0.0);
	mat4 view = look_at(vec3(0.0, 1.5, 8.0), vec3(0.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0));
	mat4 proj = projection_matrix();

	gl_Position = proj * view * vec4(world_pos, 1.0);
	fragColor = vec3(colors[src_sphere]);
	fragLocal = QUAD_VERTS[gl_VertexIndex];
}
