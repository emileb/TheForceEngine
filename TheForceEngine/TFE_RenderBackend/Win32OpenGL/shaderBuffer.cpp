#include <TFE_RenderBackend/shaderBuffer.h>
#include "gl.h"
#include <memory.h>
#include "openGL_Caps.h"
#ifdef USE_GLES
#include <SDL_video.h>
#endif

GLenum getFormat(const ShaderBufferDef& bufferDef);

#ifdef USE_GLES
// GLES 3.0 fallback: when texture buffers are unavailable, emulate them with a 2D texture of this
// width. Must match BUF_TEX_WIDTH in the shader fallback (see s_bufferFetch in shader.cpp).
static const u32 c_bufTexWidth = 2048;
static void getFormat2D(const ShaderBufferDef& bufferDef, GLenum* format, GLenum* type);

// glTexBuffer is core in GLES 3.2 but only available as glTexBufferEXT / glTexBufferOES on a 3.1
// context. The glad loader (generated for 3.2) may resolve the core symbol to null on such a
// device, so resolve a working entry point at runtime, preferring the core symbol.
typedef void (*PFN_glTexBuffer)(GLenum target, GLenum internalformat, GLuint buffer);
static PFN_glTexBuffer resolveTexBuffer()
{
	// Prefer the core symbol, then the 3.1 extension spellings.
	PFN_glTexBuffer fn = (PFN_glTexBuffer)SDL_GL_GetProcAddress("glTexBuffer");
	if (!fn) { fn = (PFN_glTexBuffer)SDL_GL_GetProcAddress("glTexBufferEXT"); }
	if (!fn) { fn = (PFN_glTexBuffer)SDL_GL_GetProcAddress("glTexBufferOES"); }
	return fn;
}
#endif

ShaderBuffer::~ShaderBuffer()
{
	destroy();
}

bool ShaderBuffer::create(u32 count, const ShaderBufferDef& bufferDef, bool dynamic, void* initData)
{
	if (!count) { return false; }
	GLenum internalFormat = getFormat(bufferDef);
	if (internalFormat == GL_INVALID_ENUM)
	{
		return false;
	}
	m_initialized = true;

	// Track shader buffer attributes.
	m_bufferDef = bufferDef;
	m_stride  = m_bufferDef.channelCount * m_bufferDef.channelSize;
	m_count   = count;
	m_size    = m_stride * m_count;
	m_dynamic = dynamic;
	m_gpuHandle[0] = 0;
	m_gpuHandle[1] = 0;

#ifdef USE_GLES
	if (!OpenGL_Caps::supportsTextureBuffer())
	{
		// GLES 3.0 fallback: store the linear buffer in a 2D texture, uploaded row by row.
		m_texWidth  = c_bufTexWidth;
		m_texHeight = (count + m_texWidth - 1) / m_texWidth;
		getFormat2D(bufferDef, (GLenum*)&m_texFormat, (GLenum*)&m_texType);

		glGenTextures(1, &m_gpuHandle[1]);
		glBindTexture(GL_TEXTURE_2D, m_gpuHandle[1]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		// Allocate the full texture; populated rows are uploaded via update().
		glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_texWidth, m_texHeight, 0, (GLenum)m_texFormat, (GLenum)m_texType, nullptr);
		glBindTexture(GL_TEXTURE_2D, 0);

		if (initData) { update(initData, m_size); }
		return true;
	}
#endif

	// Build the GPU buffer and copy the initial data.
	glGenBuffers(1, &m_gpuHandle[0]);
	glBindBuffer(GL_TEXTURE_BUFFER, m_gpuHandle[0]);
	glBufferData(GL_TEXTURE_BUFFER, m_size, initData, dynamic ? GL_STREAM_DRAW : GL_STATIC_DRAW);
	glBindBuffer(GL_TEXTURE_BUFFER, 0);

	glGenTextures(1, &m_gpuHandle[1]);
	glBindTexture(GL_TEXTURE_BUFFER, m_gpuHandle[1]);
#ifdef USE_GLES
	static PFN_glTexBuffer s_texBuffer = resolveTexBuffer();
	if (s_texBuffer) { s_texBuffer(GL_TEXTURE_BUFFER, internalFormat, m_gpuHandle[0]); }
#else
	glTexBuffer(GL_TEXTURE_BUFFER, internalFormat, m_gpuHandle[0]);
#endif
	glBindTexture(GL_TEXTURE_BUFFER, 0);
	
	return true;
}

void ShaderBuffer::destroy()
{
	if (m_initialized)
	{
		if (m_gpuHandle[1]) { glDeleteTextures(1, &m_gpuHandle[1]); }
		if (m_gpuHandle[0]) { glDeleteBuffers(1, &m_gpuHandle[0]); }
	}
	m_gpuHandle[0] = 0;
	m_gpuHandle[1] = 0;
	m_initialized = false;
}

void ShaderBuffer::update(const void* buffer, size_t size)
{
#ifdef USE_GLES
	if (!OpenGL_Caps::supportsTextureBuffer())
	{
		// Upload into the 2D emulation texture: full rows first, then the partial final row.
		const u32 count    = (u32)(size / m_stride);
		const u32 fullRows = count / m_texWidth;
		const u32 rem      = count % m_texWidth;
		const u8* data     = (const u8*)buffer;

		glBindTexture(GL_TEXTURE_2D, m_gpuHandle[1]);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		if (fullRows > 0)
		{
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_texWidth, fullRows, (GLenum)m_texFormat, (GLenum)m_texType, data);
		}
		if (rem > 0)
		{
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, fullRows, rem, 1, (GLenum)m_texFormat, (GLenum)m_texType, data + (size_t)fullRows * m_texWidth * m_stride);
		}
		glBindTexture(GL_TEXTURE_2D, 0);
		return;
	}
#endif
	glBindBuffer(GL_TEXTURE_BUFFER, m_gpuHandle[0]);
	glBufferData(GL_TEXTURE_BUFFER, size, buffer, m_dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
	glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

void ShaderBuffer::bind(s32 bindPoint) const
{
	if (bindPoint < 0) { return; }
	glActiveTexture(GL_TEXTURE0 + bindPoint);
#ifdef USE_GLES
	if (!OpenGL_Caps::supportsTextureBuffer())
	{
		glBindTexture(GL_TEXTURE_2D, m_gpuHandle[1]);
		return;
	}
#endif
	glBindTexture(GL_TEXTURE_BUFFER, m_gpuHandle[1]);
}

void ShaderBuffer::unbind(s32 bindPoint) const
{
	if (bindPoint < 0) { return; }
	glActiveTexture(GL_TEXTURE0 + bindPoint);
#ifdef USE_GLES
	if (!OpenGL_Caps::supportsTextureBuffer())
	{
		glBindTexture(GL_TEXTURE_2D, 0);
		return;
	}
#endif
	glBindTexture(GL_TEXTURE_BUFFER, 0);
}

s32 ShaderBuffer::getMaxSize()
{
#ifdef USE_GLES
	if (!OpenGL_Caps::supportsTextureBuffer())
	{
		// 2D emulation: the limit is width * GL_MAX_TEXTURE_SIZE. GLES 3.0 guarantees at least
		// 2048 for the latter; use a conservative value that comfortably exceeds engine needs.
		return (s32)(c_bufTexWidth * 2048u);
	}
#endif
	return OpenGL_Caps::getMaxTextureBufferSize();
}

GLenum getFormat(const ShaderBufferDef& bufferDef)
{
	if (bufferDef.channelCount == 1)
	{
		if (bufferDef.channelSize == 1)
		{
			return GL_R8;
		}
		else if (bufferDef.channelSize == 2)
		{
			return GL_R16;
		}
		else if (bufferDef.channelSize == 4)
		{
			if (bufferDef.channelType == BUF_CHANNEL_UINT)
			{
				return GL_R32UI;
			}
			else if (bufferDef.channelType == BUF_CHANNEL_INT)
			{
				return GL_R32I;
			}
			else if (bufferDef.channelType == BUF_CHANNEL_FLOAT)
			{
				return GL_R32F;
			}
		}
	}
	else if (bufferDef.channelCount == 2)
	{
		if (bufferDef.channelSize == 1)
		{
			return GL_RG8;
		}
		else if (bufferDef.channelSize == 2)
		{
			return GL_RG16;
		}
		else if (bufferDef.channelSize == 4)
		{
			if (bufferDef.channelType == BUF_CHANNEL_UINT)
			{
				return GL_RG32UI;
			}
			else if (bufferDef.channelType == BUF_CHANNEL_INT)
			{
				return GL_RG32I;
			}
			else if (bufferDef.channelType == BUF_CHANNEL_FLOAT)
			{
				return GL_RG32F;
			}
		}
	}
	else if (bufferDef.channelCount == 4)
	{
		if (bufferDef.channelSize == 1)
		{
			return GL_RGBA8;
		}
		else if (bufferDef.channelSize == 2)
		{
			return GL_RGBA16;
		}
		else if (bufferDef.channelSize == 4)
		{
			if (bufferDef.channelType == BUF_CHANNEL_UINT)
			{
				return GL_RGBA32UI;
			}
			else if (bufferDef.channelType == BUF_CHANNEL_INT)
			{
				return GL_RGBA32I;
			}
			else if (bufferDef.channelType == BUF_CHANNEL_FLOAT)
			{
				return GL_RGBA32F;
			}
		}
	}
	return GL_INVALID_ENUM;
}

#ifdef USE_GLES
// Maps a ShaderBufferDef to the (format, type) pair required by glTexImage2D/glTexSubImage2D for
// the GLES 3.0 2D-texture emulation. The internal format comes from getFormat() above.
static void getFormat2D(const ShaderBufferDef& bufferDef, GLenum* format, GLenum* type)
{
	const bool isInteger = (bufferDef.channelType == BUF_CHANNEL_INT || bufferDef.channelType == BUF_CHANNEL_UINT);
	switch (bufferDef.channelCount)
	{
		case 1:  *format = isInteger ? GL_RED_INTEGER  : GL_RED;  break;
		case 2:  *format = isInteger ? GL_RG_INTEGER   : GL_RG;   break;
		default: *format = isInteger ? GL_RGBA_INTEGER : GL_RGBA; break;
	}

	if (bufferDef.channelType == BUF_CHANNEL_FLOAT)
	{
		*type = GL_FLOAT;	// channelSize 4
	}
	else if (bufferDef.channelType == BUF_CHANNEL_INT)
	{
		*type = (bufferDef.channelSize == 1) ? GL_BYTE  : (bufferDef.channelSize == 2) ? GL_SHORT          : GL_INT;
	}
	else // BUF_CHANNEL_UINT
	{
		*type = (bufferDef.channelSize == 1) ? GL_UNSIGNED_BYTE : (bufferDef.channelSize == 2) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
	}
}
#endif