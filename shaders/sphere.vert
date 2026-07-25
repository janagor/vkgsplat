#version 460
#extension GL_EXT_descriptor_heap : require
#extension GL_EXT_nonuniform_qualifier : enable

// Instanced screen-space Gaussian quads (PlayCanvas-style HW raster path).
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragConic;
layout(location = 2) out vec2 fragOffset;
layout(location = 3) out float fragOpacity;

layout(push_constant) uniform RasterPush {
	vec4 camera_position;
	uvec2 viewport;
	uint tile_size;
	uint tiles_x;
	vec4 background;
	uint sh_degree;
	uint _pad0;
	uint _pad1;
	uint _pad2;
} push;

const uint HEAP_PROJECTED = 4u;
const uint HEAP_SORTED_VALUES = 8u;

const uint PROJECTED_STRIDE = 11u;
const uint PROJ_MEAN = 0u;
const uint PROJ_CONIC = 2u;
const uint PROJ_RADIUS = 6u;
const uint PROJ_COLOR = 7u;
const uint PROJ_OPACITY = 10u;

layout(descriptor_heap, std430) readonly buffer ProjectedBuffer {
	float data[];
} projected_buffers[];

layout(descriptor_heap, std430) readonly buffer SortedValuesBuffer {
	uint data[];
} sorted_values_buffers[];

const vec2 QUAD_VERTS[6] = vec2[](
	vec2(-1.0, -1.0), vec2(1.0, -1.0), vec2(-1.0, 1.0),
	vec2(-1.0, 1.0), vec2(1.0, -1.0), vec2(1.0, 1.0)
);

void main()
{
	uint gaussian_id = sorted_values_buffers[HEAP_SORTED_VALUES].data[gl_InstanceIndex];
	uint proj_base = gaussian_id * PROJECTED_STRIDE;

	vec2 mean = vec2(
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_MEAN],
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_MEAN + 1u]);
	vec3 conic = vec3(
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_CONIC],
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_CONIC + 1u],
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_CONIC + 2u]);
	float radius = projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_RADIUS];
	vec3 color = vec3(
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_COLOR],
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_COLOR + 1u],
		projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_COLOR + 2u]);
	float opacity = projected_buffers[HEAP_PROJECTED].data[proj_base + PROJ_OPACITY];

	// Degenerate / culled splats collapse to a point (fragment will discard).
	if (radius < 1.0) {
		gl_Position = vec4(2.0, 2.0, 0.0, 1.0);
		fragColor = vec3(0.0);
		fragConic = vec3(0.0);
		fragOffset = vec2(0.0);
		fragOpacity = 0.0;
		return;
	}

	vec2 local = QUAD_VERTS[gl_VertexIndex];
	vec2 offset = local * radius;
	vec2 pixel = mean + offset;

	float width = max(float(push.viewport.x), 1.0);
	float height = max(float(push.viewport.y), 1.0);
	vec2 ndc = vec2(pixel.x / width, pixel.y / height) * 2.0 - 1.0;
	gl_Position = vec4(ndc, 0.0, 1.0);

	fragColor = color;
	fragConic = conic;
	fragOffset = offset;
	fragOpacity = opacity;
}
