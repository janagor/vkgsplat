#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragConic;
layout(location = 2) in vec2 fragOffset;
layout(location = 3) in float fragOpacity;

layout(location = 0) out vec4 outColor;

const float ALPHA_MIN = 1.0 / 255.0;

void main()
{
	vec2 d = fragOffset;
	float power = -0.5 * (fragConic.x * d.x * d.x + fragConic.y * d.y * d.y)
		- fragConic.z * d.x * d.y;
	if (power > 0.0) {
		discard;
	}

	float alpha = min(0.99, fragOpacity * exp(power));
	if (alpha < ALPHA_MIN) {
		discard;
	}

	// Standard over operator via ROP: C' = C_src * α + C_dst * (1 - α)
	outColor = vec4(fragColor, alpha);
}
