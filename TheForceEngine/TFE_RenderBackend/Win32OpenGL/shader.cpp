#include "glslParser.h"
#include <TFE_RenderBackend/shader.h>
#include <TFE_RenderBackend/vertexBuffer.h>
#include <TFE_System/system.h>
#include <TFE_FileSystem/filestream.h>
#include <TFE_FileSystem/paths.h>
#include <TFE_RenderBackend/renderBackend.h>
#ifdef USE_GLES
#include <TFE_RenderBackend/Win32OpenGL/openGL_Caps.h>
#endif
#include "gl.h"
#include <assert.h>
#include <vector>
#include <string>
#include <SDL.h> 

namespace ShaderGL
{
	static const char* c_shaderAttrName[]=
	{
		"vtx_pos",   // ATTR_POS
		"vtx_nrm",   // ATTR_NRM
		"vtx_uv",    // ATTR_UV
		"vtx_uv1",   // ATTR_UV1
		"vtx_uv2",   // ATTR_UV2
		"vtx_uv3",   // ATTR_UV3
		"vtx_color", // ATTR_COLOR
	};

	static const s32 c_glslVersion[] = { 130, 330, 450 };
#ifdef USE_GLES
	// GLES 3.1 baseline. Texture buffers (samplerBuffer) are not core until 3.2, so on a 3.1
	// context they are enabled per-shader via GL_EXT_texture_buffer (see s_ext_texture_buffer).
	static const GLchar* c_glslVersionString[] = { "#version 310 es\n", "#version 310 es\n", "#version 310 es\n" };
#else
	static const GLchar* c_glslVersionString[] = { "#version 130\n", "#version 330\n", "#version 450\n" };
#endif
	static std::vector<char> s_buffers[2];
	static std::string s_defineString;
	static std::string s_vertexFile, s_fragmentFile;

	// If you get an error please report on github. You may try different GL context version or GLSL version. See GL<>GLSL version table at the top of this file.
	bool CheckShader(GLuint handle, const char* desc)
	{
		GLint status = 0, log_length = 0;
		glGetShaderiv(handle, GL_COMPILE_STATUS, &status);
		glGetShaderiv(handle, GL_INFO_LOG_LENGTH, &log_length);
		if ((GLboolean)status == GL_FALSE)
		{
			TFE_System::logWrite(LOG_ERROR, "Shader", "Failed to compile '%s'!\n", desc);
		}

		if (log_length > 1)
		{
			std::vector<char> buf;
			buf.resize(size_t(log_length + 1));
			glGetShaderInfoLog(handle, log_length, NULL, (GLchar*)buf.data());
			TFE_System::logWrite(LOG_ERROR, "Shader", "Error: %s\n", buf.data());
		}
		return (GLboolean)status == GL_TRUE;
	}
}

// Stage defines so shaders (e.g. clipping.h) can distinguish vertex vs fragment context.
static const char* s_vertexShaderDefine   = "#define VERTEX_SHADER\n";
static const char* s_fragmentShaderDefine = "#define FRAGMENT_SHADER\n";

#ifdef USE_GLES
// Extension and precision strings injected into GLES shaders.
static const char* s_ext_OES_standard_derivatives  = "#extension GL_OES_standard_derivatives : enable\n";
static const char* s_ext_EXT_clip_cull_distance     = "#extension GL_EXT_clip_cull_distance : enable\n";
static const char* s_ext_NV_noperspective            = "#extension GL_NV_shader_noperspective_interpolation : enable\n";
// Texture buffers are core in GLES 3.2; on a 3.1 context they require an extension. Enable both
// the EXT and OES spellings ('enable' only warns if unsupported, so blit-only/3.0 shaders that
// never reference samplerBuffer still compile).
static const char* s_ext_texture_buffer             = "#extension GL_EXT_texture_buffer : enable\n#extension GL_OES_texture_buffer : enable\n";
static const char* s_noperspective_define            = "#define NOPERSPECTIVE noperspective\n";
static const char* s_no_noperspective_define         = "#define NOPERSPECTIVE\n";
static const char* s_defaultPrecisions = R"(
	precision highp int;
	precision highp float;
	precision highp sampler2D;
	precision highp usampler2D;
	precision highp isampler2D;
	precision highp sampler3D;
	precision highp samplerCube;
	precision highp sampler2DArray;
	precision highp sampler2DShadow;
	precision highp samplerCubeShadow;
	precision highp sampler2DArrayShadow;
)";
// samplerBuffer precision qualifiers are only valid when texture buffers are available, so they
// are injected separately (gated on OpenGL_Caps::supportsTextureBuffer()).
static const char* s_textureBufferPrecisions = R"(
	precision highp samplerBuffer;
	precision highp isamplerBuffer;
	precision highp usamplerBuffer;
)";
#else
// On desktop GL, precision qualifiers are not used; define them away.
static const char* s_defaultPrecisions = R"(
	#define highp
	#define mediump
	#define lowp
)";
static const char* s_noperspective_define = "#define NOPERSPECTIVE noperspective\n";
#endif

// Shared texture-buffer fetch abstraction. Buffer samplers are accessed via texelFetchBuf(). On a
// device with texture buffers (GLES 3.1+/desktop) it maps to the native samplerBuffer/texelFetch.
// On GLES 3.0 (no texture buffers), SHADER_BUFFER_2D is defined and the linear buffer is emulated
// with a 2D data texture of width BUF_TEX_WIDTH (must match c_bufTexWidth in shaderBuffer.cpp).
static const char* s_bufferFetch = R"(
#ifdef SHADER_BUFFER_2D
	#ifndef BUF_TEX_WIDTH
	#define BUF_TEX_WIDTH 2048
	#endif
	#define samplerBuffer sampler2D
	#define isamplerBuffer isampler2D
	#define usamplerBuffer usampler2D
	vec4  texelFetchBuf(sampler2D s, int i)  { return texelFetch(s, ivec2(i % BUF_TEX_WIDTH, i / BUF_TEX_WIDTH), 0); }
	ivec4 texelFetchBuf(isampler2D s, int i) { return texelFetch(s, ivec2(i % BUF_TEX_WIDTH, i / BUF_TEX_WIDTH), 0); }
	uvec4 texelFetchBuf(usampler2D s, int i) { return texelFetch(s, ivec2(i % BUF_TEX_WIDTH, i / BUF_TEX_WIDTH), 0); }
#elif defined(GL_ES) || __VERSION__ >= 140
	// samplerBuffer requires GLSL 1.40+ (desktop) or GLES; omit on the GLSL 130 compatible path,
	// where these helpers are unused (blit/2D shaders never call texelFetchBuf).
	vec4  texelFetchBuf(samplerBuffer s, int i)  { return texelFetch(s, i); }
	ivec4 texelFetchBuf(isamplerBuffer s, int i) { return texelFetch(s, i); }
	uvec4 texelFetchBuf(usamplerBuffer s, int i) { return texelFetch(s, i); }
#endif
)";
#ifdef USE_GLES
static const char* s_shaderBuffer2DDefine = "#define SHADER_BUFFER_2D 1\n";
#endif

bool Shader::create(const char* vertexShaderGLSL, const char* fragmentShaderGLSL, const char* defineString/* = nullptr*/, ShaderVersion version/* = SHADER_VER_COMPTABILE*/)
{
	// Create shaders
	m_shaderVersion = version;

#ifdef USE_GLES
	// GLES: build shader parts dynamically to inject version, extensions, and precision qualifiers.
	// Texture buffers require a 3.1 baseline (#version 310 es + GL_EXT_texture_buffer); when they are
	// unavailable we target #version 300 es and emulate buffers with 2D textures (the GLES 3.0 path).
	const GLchar* glesVersion = OpenGL_Caps::supportsTextureBuffer() ? ShaderGL::c_glslVersionString[m_shaderVersion] : "#version 300 es\n";
	u32 vertHandle = glCreateShader(GL_VERTEX_SHADER);
	{
		std::vector<const GLchar*> parts;
		parts.push_back(glesVersion);
		if (OpenGL_Caps::supportsTextureBuffer())
			parts.push_back(s_ext_texture_buffer);
		if (OpenGL_Caps::supportsClipping())
			parts.push_back(s_ext_EXT_clip_cull_distance);
		if (OpenGL_Caps::supportsNoPerspectiveInterpolation())
		{
			parts.push_back(s_ext_NV_noperspective);
			parts.push_back(s_noperspective_define);
		}
		else
		{
			parts.push_back(s_no_noperspective_define);
		}
		parts.push_back(s_defaultPrecisions);
		if (OpenGL_Caps::supportsTextureBuffer())
			parts.push_back(s_textureBufferPrecisions);
		else
			parts.push_back(s_shaderBuffer2DDefine);
		parts.push_back(s_bufferFetch);
		parts.push_back(s_vertexShaderDefine);
		if (defineString) parts.push_back(defineString);
		parts.push_back(vertexShaderGLSL);
		glShaderSource(vertHandle, (GLsizei)parts.size(), parts.data(), nullptr);
	}
	glCompileShader(vertHandle);
	if (!ShaderGL::CheckShader(vertHandle, ShaderGL::s_vertexFile.c_str())) { return false; }

	u32 fragHandle = glCreateShader(GL_FRAGMENT_SHADER);
	{
		std::vector<const GLchar*> parts;
		parts.push_back(glesVersion);
		parts.push_back(s_ext_OES_standard_derivatives);
		if (OpenGL_Caps::supportsTextureBuffer())
			parts.push_back(s_ext_texture_buffer);
		if (OpenGL_Caps::supportsNoPerspectiveInterpolation())
		{
			parts.push_back(s_ext_NV_noperspective);
			parts.push_back(s_noperspective_define);
		}
		else
		{
			parts.push_back(s_no_noperspective_define);
		}
		parts.push_back(s_defaultPrecisions);
		if (OpenGL_Caps::supportsTextureBuffer())
			parts.push_back(s_textureBufferPrecisions);
		else
			parts.push_back(s_shaderBuffer2DDefine);
		parts.push_back(s_bufferFetch);
		parts.push_back(s_fragmentShaderDefine);
		if (defineString) parts.push_back(defineString);
		parts.push_back(fragmentShaderGLSL);
		glShaderSource(fragHandle, (GLsizei)parts.size(), parts.data(), nullptr);
	}
	glCompileShader(fragHandle);
    GLint success = 1;
	if (!ShaderGL::CheckShader(fragHandle, ShaderGL::s_fragmentFile.c_str()))
        success = 0;
#else
	// Desktop GL path (with macOS version override).
	const GLchar* version_string;
	if (strcmp(SDL_GetPlatform(), "Mac OS X") == 0) {
		// Force GLSL version 410 for macOS
		version_string = "#version 410\n";
	} else {
		version_string = ShaderGL::c_glslVersionString[m_shaderVersion];
	}

	const GLchar *vertex_shader_with_version[7] = { version_string, s_defaultPrecisions, s_noperspective_define, s_bufferFetch, s_vertexShaderDefine, defineString ? defineString : "", vertexShaderGLSL };
	u32 vertHandle = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertHandle, 7, vertex_shader_with_version, NULL);
	glCompileShader(vertHandle);

	GLint success = 0;
	glGetShaderiv(vertHandle, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		GLchar infoLog[512];
		glGetShaderInfoLog(vertHandle, 512, NULL, infoLog);
		TFE_System::logWrite(LOG_ERROR, "Shader", "Vertex shader compilation failed:\n%s", infoLog);
		return false;
	}

	const GLchar *fragment_shader_with_version[7] = { version_string, s_defaultPrecisions, s_noperspective_define, s_bufferFetch, s_fragmentShaderDefine, defineString ? defineString : "", fragmentShaderGLSL };
	u32 fragHandle = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragHandle, 7, fragment_shader_with_version, NULL);
	glCompileShader(fragHandle);
	glGetShaderiv(fragHandle, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		GLchar infoLog[512];
		glGetShaderInfoLog(fragHandle, 512, NULL, infoLog);
		TFE_System::logWrite(LOG_ERROR, "Shader", "Fragment shader compilation failed:\n%s", infoLog);
		return false;
	}
#endif

	m_gpuHandle = glCreateProgram();
	glAttachShader(m_gpuHandle, vertHandle);
	glAttachShader(m_gpuHandle, fragHandle);
	// Bind vertex attribute names to slots.
	for (u32 i = 0; i < ATTR_COUNT; i++)
	{
		glBindAttribLocation(m_gpuHandle, i, ShaderGL::c_shaderAttrName[i]);
	}

	glLinkProgram(m_gpuHandle);

	glGetProgramiv(m_gpuHandle, GL_LINK_STATUS, &success);
	if (!success)
	{
		GLchar infoLog[512];
		glGetProgramInfoLog(m_gpuHandle, 512, NULL, infoLog);
		TFE_System::logWrite(LOG_ERROR, "Shader", "Shader program linking failed:\n%s", infoLog);
		return false;
	}

	// Clean up shader objects
	glDeleteShader(vertHandle);
	glDeleteShader(fragHandle);

	return m_gpuHandle != 0;
}

bool Shader::load(const char* vertexShaderFile, const char* fragmentShaderFile, u32 defineCount/* = 0*/, ShaderDefine* defines/* = nullptr*/, ShaderVersion version/* = SHADER_VER_COMPTABILE*/)
{
	m_shaderVersion = version;
	ShaderGL::s_buffers[0].clear();
	ShaderGL::s_buffers[1].clear();

	GLSLParser::parseFile(vertexShaderFile,   ShaderGL::s_buffers[0]);
	GLSLParser::parseFile(fragmentShaderFile, ShaderGL::s_buffers[1]);

	ShaderGL::s_vertexFile = vertexShaderFile;
	ShaderGL::s_fragmentFile = fragmentShaderFile;

	ShaderGL::s_buffers[0].push_back(0);
	ShaderGL::s_buffers[1].push_back(0);

	// Build a string of defines.
	ShaderGL::s_defineString.clear();
	if (defineCount)
	{
		ShaderGL::s_defineString += "\r\n";
		for (u32 i = 0; i < defineCount; i++)
		{
			ShaderGL::s_defineString += "#define ";
			ShaderGL::s_defineString += defines[i].name;
			ShaderGL::s_defineString += " ";
			ShaderGL::s_defineString += defines[i].value;
			ShaderGL::s_defineString += "\r\n";
		}
		ShaderGL::s_defineString += "\r\n";
	}

	return create(ShaderGL::s_buffers[0].data(), ShaderGL::s_buffers[1].data(), ShaderGL::s_defineString.c_str(), m_shaderVersion);
}

void Shader::enableClipPlanes(s32 count)
{
	m_clipPlaneCount = count;
}

void Shader::destroy()
{
	if (m_gpuHandle)
	{
		glDeleteProgram(m_gpuHandle);
	}
	m_gpuHandle = 0;
}

void Shader::bind()
{
	TFE_RenderBackend::bindGlobalVAO();	// for macOS GL
	glUseProgram(m_gpuHandle);
	TFE_RenderState::enableClipPlanes(m_clipPlaneCount);
}

void Shader::unbind()
{
	glUseProgram(0);
}

s32 Shader::getVariableId(const char* name)
{
	return glGetUniformLocation(m_gpuHandle, name);
}

// For debugging.
s32 Shader::getVariables()
{
	s32 length;
	s32 size;
	GLenum type;
	char name[256];

	s32 count;
	glGetProgramiv(m_gpuHandle, GL_ACTIVE_UNIFORMS, &count);
	printf("Active Uniforms: %d\n", count);

	for (s32 i = 0; i < count; i++)
	{
		glGetActiveUniform(m_gpuHandle, (GLuint)i, 256, &length, &size, &type, name);
		printf("Uniform #%d Type: %u Name: %s\n", i, type, name);
	}

	s32 attribCount;
	glGetProgramiv(m_gpuHandle, GL_ACTIVE_ATTRIBUTES, &attribCount);
	printf("Active Attributes: %d\n", attribCount);

	for (s32 i = 0; i < attribCount; i++)
	{
		glGetActiveAttrib(m_gpuHandle, (GLuint)i, 256, &length, &size, &type, name);
		printf("Attribute #%d Type: %u Name: %s\n", i, type, name);
	}

	return count;
}

void Shader::bindTextureNameToSlot(const char* texName, s32 slot)
{
	const s32 curSlot = glGetUniformLocation(m_gpuHandle, texName);
	if (curSlot < 0 || slot < 0) { return; }

	bind();
	glUniform1i(curSlot, slot);
	unbind();
}

void Shader::setVariable(s32 id, ShaderVariableType type, const f32* data)
{
	if (id < 0) { return; }

	switch (type)
	{
	case SVT_SCALAR:
		glUniform1f(id, data[0]);
		break;
	case SVT_VEC2:
		glUniform2fv(id, 1, data);
		break;
	case SVT_VEC3:
		glUniform3fv(id, 1, data);
		break;
	case SVT_VEC4:
		glUniform4fv(id, 1, data);
		break;
	case SVT_MAT3x3:
		glUniformMatrix3fv(id, 1, false, data);
		break;
	case SVT_MAT4x3:
		glUniformMatrix4x3fv(id, 1, false, data);
		break;
	case SVT_MAT4x4:
		glUniformMatrix4fv(id, 1, false, data);
		break;
	default:
		TFE_System::logWrite(LOG_ERROR, "Shader", "Mismatched parameter type.");
		assert(0);
	}
}

void Shader::setVariableArray(s32 id, ShaderVariableType type, const f32* data, u32 count)
{
	if (id < 0) { return; }

	switch (type)
	{
	case SVT_SCALAR:
		glUniform1fv(id, count, data);
		break;
	case SVT_VEC2:
		glUniform2fv(id, count, data);
		break;
	case SVT_VEC3:
		glUniform3fv(id, count, data);
		break;
	case SVT_VEC4:
		glUniform4fv(id, count, data);
		break;
	case SVT_MAT3x3:
		glUniformMatrix3fv(id, count, false, data);
		break;
	case SVT_MAT4x3:
		glUniformMatrix4x3fv(id, count, false, data);
		break;
	case SVT_MAT4x4:
		glUniformMatrix4fv(id, count, false, data);
		break;
	default:
		TFE_System::logWrite(LOG_ERROR, "Shader", "Mismatched parameter type.");
		assert(0);
	}
}

void Shader::setVariable(s32 id, ShaderVariableType type, const s32* data)
{
	if (id < 0) { return; }

	switch (type)
	{
	case SVT_ISCALAR:
		glUniform1i(id, *(&data[0]));
		break;
	case SVT_IVEC2:
		glUniform2iv(id, 1, data);
		break;
	case SVT_IVEC3:
		glUniform3iv(id, 1, data);
		break;
	case SVT_IVEC4:
		glUniform4iv(id, 1, data);
		break;
	default:
		TFE_System::logWrite(LOG_ERROR, "Shader", "Mismatched parameter type.");
		assert(0);
	}
}

void Shader::setVariable(s32 id, ShaderVariableType type, const u32* data)
{
	if (id < 0) { return; }

	switch (type)
	{
	case SVT_USCALAR:
		glUniform1ui(id, *(&data[0]));
		break;
	case SVT_UVEC2:
		glUniform2uiv(id, 1, data);
		break;
	case SVT_UVEC3:
		glUniform3uiv(id, 1, data);
		break;
	case SVT_UVEC4:
		glUniform4uiv(id, 1, data);
		break;
	default:
		TFE_System::logWrite(LOG_ERROR, "Shader", "Mismatched parameter type.");
		assert(0);
	}
}
