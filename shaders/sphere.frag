#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) flat in vec3 fragColor;
layout(location = 1) in vec2 fragCorner;// unit-disk UV in [-1, 1]
layout(location = 2) flat in float fragOpacity;

layout(location = 0) out vec4 outColor;

// SuperSplat / PlayCanvas gaussian on the oriented unit disk (not pixel-space EWA).
const float EXP4 = exp(-4.0);
const float INV_EXP4 = 1.0 / (1.0 - EXP4);
const float ALPHA_MIN = 1.0 / 255.0;

float norm_exp(float a)
{
	return (exp(a * -4.0) - EXP4) * INV_EXP4;
}

void main()
{
	float a = dot(fragCorner, fragCorner);
	if (a > 1.0) {
		discard;
	}

	float alpha = min(0.99, norm_exp(a) * fragOpacity);
	if (alpha < ALPHA_MIN) {
		discard;
	}

	// Premultiplied alpha — matches SuperSplat (ONE / ONE_MINUS_SRC_ALPHA).
	outColor = vec4(fragColor * alpha, alpha);
}
