#include "openGL_Caps.h"
#include "gl.h"
#include <assert.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <SDL_video.h>

enum CapabilityFlags
{
	CAP_PBO = (1 << 0),
	CAP_VBO = (1 << 1),
	CAP_FBO = (1 << 2),
	CAP_UBO = (1 << 3),
	CAP_NON_POW_2 = (1 << 4),
	CAP_TEXTURE_ARRAY = (1 << 5),
	CAP_ANISO = (1 << 6),
	CAP_CLIP_DISTANCES = (1 << 7),
	CAP_NOPERSPECTIVE_INTERPOLATION = (1 << 8),
	CAP_TEXTURE_BUFFER = (1 << 9),

	CAP_2_1_FULL = (CAP_VBO | CAP_PBO | CAP_NON_POW_2),
	CAP_3_3_FULL = (CAP_PBO | CAP_VBO | CAP_FBO | CAP_UBO | CAP_NON_POW_2 | CAP_TEXTURE_ARRAY)
};

namespace OpenGL_Caps
{
	static u32 m_supportFlags = 0;
	static u32 m_deviceTier = 0;
	static s32 m_textureBufferMaxSize = 0;
	static f32 m_maxAnisotropy = 1.0f;
	static s32 m_maxClipDistances = 0;
	static bool m_isGLES2 = false;
	// 0 = auto, 20 = force GLES 2.0, 30 = force GLES 3.0 (see openGL_Caps.h).
	static s32 m_forceGLESVersion = 0;

	enum SpecMinimum
	{
		GLSPEC_MAX_TEXTURE_BUFFER_SIZE_MIN = 65536,
	};

	void setForceGLESVersion(s32 version)
	{
		m_forceGLESVersion = version;
	}

	s32 getForceGLESVersion()
	{
		return m_forceGLESVersion;
	}

	void queryCapabilities()
	{
		GLint gl_maj = 0, gl_min = 0;

		m_supportFlags = 0;
		m_deviceTier = DEV_TIER_0;
		m_isGLES2 = false;
		glGetIntegerv(GL_MAJOR_VERSION, &gl_maj);
		glGetIntegerv(GL_MINOR_VERSION, &gl_min);

#ifdef USE_GLES
		// GL_MAJOR_VERSION/GL_MINOR_VERSION are only defined on GLES 3.0+; on a GLES 2.0 context the
		// queries above raise GL_INVALID_ENUM and leave gl_maj == 0. Parse GL_VERSION ("OpenGL ES X.Y")
		// to reliably recover the version so we can detect (and support) the GLES 2.0 fallback.
		(void)glGetError();
		if (gl_maj == 0)
		{
			const char* verStr = (const char*)glGetString(GL_VERSION);
			if (verStr)
			{
				const char* p = strstr(verStr, "ES ");
				if (p) { p += 3; sscanf(p, "%d.%d", &gl_maj, &gl_min); }
			}
		}

		// --force_gles20 / --force_gles30: clamp the detected version so the rest of the capability
		// detection behaves as if the device were that GLES level, even when the driver hands back a
		// higher context (Android drivers commonly return an ES 3.x context/version string even when a
		// 2.0 context was requested). This is what actually demotes the device tier and locks the
		// engine to the matching path - requesting the lower context alone is not enough.
		if (m_forceGLESVersion == 20)      { gl_maj = 2; gl_min = 0; }
		else if (m_forceGLESVersion == 30) { gl_maj = 3; gl_min = 0; }

		m_isGLES2 = (gl_maj > 0 && gl_maj < 3);
#endif

		bool isMacOS = (strcmp(SDL_GetPlatform(), "Mac OS X") == 0);

		// Texture buffer objects (samplerBuffer) are core in GLES 3.2 / GL 3.1+, and otherwise
		// available on GLES 3.1 via GL_EXT_texture_buffer (or GL_OES_texture_buffer). They are
		// required by the GPU renderer to upload the sector/wall/texture tables.
		// Pass --force_gles30 on the command line to force the GLES 3.0 (2D-texture) buffer emulation
		// path, even on hardware that natively supports texture buffers.
		const bool forceGLES30 = (m_forceGLESVersion == 30);
		bool texBufferCore = (gl_maj > 3) || (gl_maj == 3 && gl_min >= 2);
		if (!forceGLES30 && (texBufferCore ||
			SDL_GL_ExtensionSupported("GL_EXT_texture_buffer") ||
			SDL_GL_ExtensionSupported("GL_OES_texture_buffer") ||
			SDL_GL_ExtensionSupported("GL_ARB_texture_buffer_object")))
		{
			m_supportFlags |= CAP_TEXTURE_BUFFER;
		}

		if (isMacOS && gl_maj >= 4) {
			m_supportFlags = CAP_PBO | CAP_VBO | CAP_FBO | CAP_UBO | CAP_NON_POW_2 | CAP_TEXTURE_ARRAY;

			if (SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic")) {
				m_supportFlags |= CAP_ANISO;
			}

			// Get texture buffer maximum size
			glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &m_textureBufferMaxSize);

			// Get max anisotropy if supported
			if (m_supportFlags & CAP_ANISO) {
				glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &m_maxAnisotropy);
			} else {
				m_maxAnisotropy = 0.0f;
			}

			// Set to Tier 2 as macOS OpenGL tops out at 4.1
			if (m_textureBufferMaxSize >= GLSPEC_MAX_TEXTURE_BUFFER_SIZE_MIN) {
				m_deviceTier = DEV_TIER_2;
			}
			
			return;
		}
		
		if (SDL_GL_ExtensionSupported("GL_ARB_pixel_buffer_object"))
			m_supportFlags |= CAP_PBO | CAP_NON_POW_2;
		if (SDL_GL_ExtensionSupported("GL_ARB_vertex_buffer_object"))
			m_supportFlags |= CAP_VBO;
		if (SDL_GL_ExtensionSupported("GL_ARB_framebuffer_object"))
			m_supportFlags |= CAP_FBO;
		if (SDL_GL_ExtensionSupported("GL_ARB_uniform_buffer_object"))
			m_supportFlags |= CAP_UBO;
		if (SDL_GL_ExtensionSupported("GL_EXT_texture_array"))
			m_supportFlags |= CAP_TEXTURE_ARRAY;
		if (SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic"))
			m_supportFlags |= CAP_ANISO;

		// Get texture buffer maximum size.
		if(m_supportFlags & CAP_TEXTURE_BUFFER)
            glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &m_textureBufferMaxSize);

		if (m_supportFlags & CAP_ANISO)
		{
			glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &m_maxAnisotropy);
		}
		else
		{
			m_maxAnisotropy = 0.0f;
		}

#ifdef USE_GLES
		if (SDL_GL_ExtensionSupported("GL_EXT_clip_cull_distance"))
#endif
		{
			glGetIntegerv(GL_MAX_CLIP_DISTANCES, &m_maxClipDistances);
			if (m_maxClipDistances >= 8)
			{
				m_supportFlags |= CAP_CLIP_DISTANCES;
			}
			else
			{
				m_maxClipDistances = 8;
			}
		}
#ifdef USE_GLES
		else
		{
			//TFE_System::logWrite(LOG_WARNING, "OpenGL_Caps", "GL_EXT_clip_cull_distance not supported, using SW clipping instead.");
			m_maxClipDistances = 8;
		}
#endif

#ifdef USE_GLES
		if (SDL_GL_ExtensionSupported("GL_NV_shader_noperspective_interpolation"))
#endif
		{
			m_supportFlags |= CAP_NOPERSPECTIVE_INTERPOLATION;
		}
#ifdef USE_GLES
		else
		{
	//		TFE_System::logWrite(LOG_WARNING, "OpenGL_Caps", "GL_NV_shader_noperspective_interpolation not supported, some rendering may look incorrect.");
		}
#endif

		// clear any pending errors.
		(void)glGetError();

		if ((gl_maj >= 4) && (gl_min >= 5) &&
		    (m_textureBufferMaxSize >= GLSPEC_MAX_TEXTURE_BUFFER_SIZE_MIN))
		{
			m_deviceTier = DEV_TIER_3;
		}
		else if (((m_supportFlags & CAP_3_3_FULL) == CAP_3_3_FULL) &&
			 (m_textureBufferMaxSize >= GLSPEC_MAX_TEXTURE_BUFFER_SIZE_MIN))
		{
			m_deviceTier = DEV_TIER_2;
		}
		else if ((m_supportFlags & CAP_2_1_FULL) == CAP_2_1_FULL)
		{
			m_deviceTier = DEV_TIER_1;
		}

#ifdef USE_GLES
		if (m_isGLES2)
		{
			// GLES 2.0 (GLSL ES 1.00): the GPU renderer relies on GLES 3 features (texelFetch,
			// integer samplers, texture buffers/arrays, MRT) that do not exist here. Cap the device
			// at Tier 1 so only the software renderer + GPU blit path is used.
			m_deviceTier = DEV_TIER_1;
		}
		// On GLES, the GPU renderer needs GL_OES_standard_derivatives (fwidth/dFdx). Texture buffers
		// are used when available (supportsTextureBuffer()); otherwise the 2D-texture emulation in
		// shaderBuffer.cpp provides the same data access on GLES 3.0, so they are NOT required to
		// enable the GPU-renderer tier. Devices without derivatives fall back to blit/software.
		else if (SDL_GL_ExtensionSupported("GL_OES_standard_derivatives"))
		{
			m_deviceTier = DEV_TIER_3;
		}
		else
		{
			//TFE_System::logWrite(LOG_ERROR, "OpenGL_Caps", "GL_OES_standard_derivatives not supported on this GLES device!");
		}
#endif
	}

	bool supportsPbo()
	{
		return (m_supportFlags&CAP_PBO) != 0;
	}
	
	bool supportsVbo()
	{
		return (m_supportFlags&CAP_VBO) != 0;
	}

	bool supportsFbo()
	{
		return (m_supportFlags&CAP_FBO) != 0;
	}

	bool supportsNonPow2Textures()
	{
		return (m_supportFlags&CAP_NON_POW_2) != 0;
	}

	bool supportsTextureArrays()
	{
		return (m_supportFlags&CAP_TEXTURE_ARRAY) != 0;
	}

	bool supportsAniso()
	{
		return (m_supportFlags & CAP_ANISO) != 0;
	}

	bool supportsClipping()
	{
		return (m_supportFlags & CAP_CLIP_DISTANCES) != 0;
	}

	bool supportsNoPerspectiveInterpolation()
	{
		return (m_supportFlags & CAP_NOPERSPECTIVE_INTERPOLATION) != 0;
	}

	bool supportsTextureBuffer()
	{
		return (m_supportFlags & CAP_TEXTURE_BUFFER) != 0;
	}

	bool isGLES2()
	{
		return m_isGLES2;
	}

	bool deviceSupportsGpuBlit()
	{
		return m_deviceTier > DEV_TIER_0;
	}

	bool deviceSupportsGpuColorConversion()
	{
		return m_deviceTier > DEV_TIER_1;
	}

	bool deviceSupportsGpuRenderer()
	{
		return m_deviceTier > DEV_TIER_1;
	}

	s32 getMaxTextureBufferSize()
	{
		return m_textureBufferMaxSize;
	}

	f32 getMaxAnisotropy()
	{
		return m_maxAnisotropy;
	}

	f32 getAnisotropyFromQuality(f32 quality)
	{
		return std::max(1.0f, floorf(quality * m_maxAnisotropy + 0.1f));
	}

	u32 getDeviceTier()
	{
		return m_deviceTier;
	}
}
