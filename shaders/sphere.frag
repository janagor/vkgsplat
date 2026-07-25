#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) flat in vec3 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) flat in float fragOpacity;

layout(location = 0) out vec4 outColor;

// PlayCanvas gsplatPS: gaussian falloff in unit UV disk of the oriented quad.
const float EXP4 = exp(-4.0);
const float INV_EXP4 = 1.0 / (1.0 - EXP4);
const float ALPHA_MIN = 1.0 / 255.0;

float norm_exp(float a)
{
	return (exp(a * -4.0) - EXP4) * INV_EXP4;
}

void main()
{
	float a = dot(fragUV, fragUV);
	if (a > 1.0) {
		discard;
	}

	float alpha = norm_exp(a) * fragOpacity;
	if (alpha < ALPHA_MIN) {
		discard;
	}

	// Premultiplied over (matches PlayCanvas BLEND_PREMULTIPLIED).
	outColor = vec4(fragColor * alpha, alpha);
}
