#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragLocal;
layout(location = 2) in float fragOpacity;

layout(location = 0) out vec4 outColor;

void main()
{
	float dist_sq = dot(fragLocal, fragLocal);
	if (dist_sq > 1.0) { discard; }

	float shade = sqrt(max(0.0, 1.0 - dist_sq));
	float alpha = fragOpacity * shade;
	if (alpha < 1.0 / 255.0) { discard; }

	vec3 lit = fragColor * (0.35 + 0.65 * shade) * alpha;
	outColor = vec4(lit, 1.0);
}
