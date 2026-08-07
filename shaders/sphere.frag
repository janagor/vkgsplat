#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) flat in vec3 fragColor;
layout(location = 1) flat in vec2 fragMean;
layout(location = 2) flat in vec3 fragConic;
layout(location = 3) flat in float fragOpacity;

layout(location = 0) out vec4 outColor;

const float ALPHA_MIN = 1.0 / 255.0;

void main()
{
	vec2 pix = gl_FragCoord.xy + vec2(0.5);
	vec2 delta = fragMean - pix;

	// conic = inverse Σ₂D (xx, xy, yy)
	float power = -0.5 * (fragConic.x * delta.x * delta.x + fragConic.z * delta.y * delta.y)
		- fragConic.y * delta.x * delta.y;
	if (power > 0.0) {
		discard;
	}

	float alpha = min(0.99, fragOpacity * exp(power));
	if (alpha < ALPHA_MIN) {
		discard;
	}

	// Premultiplied alpha — matches SuperSplat (ONE / ONE_MINUS_SRC_ALPHA).
	outColor = vec4(fragColor * alpha, alpha);
}
