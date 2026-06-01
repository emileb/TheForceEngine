#include "Shaders/clipping.h"

uniform vec3 CameraPos;
uniform vec3 CameraRight;
uniform mat3 CameraView;
uniform mat4 CameraProj;

uniform isamplerBuffer TextureTable;
uniform samplerBuffer DrawListPosXZ_Texture;
uniform samplerBuffer DrawListPosYU_Texture;
uniform isamplerBuffer DrawListTexId_Texture;

uniform samplerBuffer DrawListPlanes;

// in int gl_VertexID;
out vec2 Frag_Uv; // base uv coordinates (0 - 1)
out vec3 Frag_Pos;     // camera relative position for lighting.
flat out vec4 Texture_Data; // not much here yet.
flat out int Frag_TextureId;

void unpackPortalInfo(uint portalInfo, out uint portalOffset, out uint portalCount)
{
	portalCount  = (portalInfo >> 16u) & 15u;
	portalOffset = portalInfo & 65535u;
}

void main()
{
	// We do our own vertex fetching and geometry expansion, so calculate the relevent values from the vertex ID.
	int spriteIndex = gl_VertexID / 4;
	int vertexId  = gl_VertexID & 3;
	
	vec4 posTextureXZ = texelFetchBuf(DrawListPosXZ_Texture, spriteIndex);
	vec4 posTextureYU = texelFetchBuf(DrawListPosYU_Texture, spriteIndex);
	uvec2 texPortalData = uvec2(texelFetchBuf(DrawListTexId_Texture, spriteIndex).rg);
	uint tex_flags = texPortalData.x;
	Frag_TextureId = int(tex_flags & 32767u);

	float u = float(vertexId&1);
	float v = float(1-(vertexId/2));

	vec3 vtx_pos;
	vtx_pos.xz = mix(posTextureXZ.xy, posTextureXZ.zw, u);
	vtx_pos.y  = mix(posTextureYU.x, posTextureYU.y, v);

	ivec2 sh = texelFetchBuf(TextureTable, Frag_TextureId).yw;
	float scaleFactor = 1.0 / float(sh.x >> 12);

	vec2 vtx_uv;
	vtx_uv.x = mix(posTextureYU.z, posTextureYU.w, u);
	vtx_uv.y = v * float(sh.y) * scaleFactor;

	vec4 texture_data = vec4(0.0);
	texture_data.y = float((tex_flags >> 16u) & 31u);

	// Calculate vertical clipping.
	uint portalInfo = texPortalData.y;
	uint portalOffset, portalCount;
	unpackPortalInfo(portalInfo, portalOffset, portalCount);

	// Clipping.
	Frag_ClipDistance[7] = 1.0; // ensure SW-clip array is explicitly sized (indexed by integral constant) for GLES.
	for (int i = 0; i < int(portalCount) && i < 8; i++)
	{
		vec4 plane = texelFetchBuf(DrawListPlanes, int(portalOffset) + i);
		Frag_ClipDistance[i] = dot(vec4(vtx_pos.xyz, 1.0), plane);
	}
	for (int i = int(portalCount); i < 8; i++)
	{
		Frag_ClipDistance[i] = 1.0;
	}

	// Relative position
	Frag_Pos = vtx_pos - CameraPos;
	
	// Transform from world to view space.
    vec3 vpos = (vtx_pos - CameraPos) * CameraView;
	gl_Position = vec4(vpos, 1.0) * CameraProj;
	
	// Write out the per-vertex uv and color.
	Frag_Uv = vtx_uv;
	Texture_Data = texture_data;
}
