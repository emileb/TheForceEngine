#include "openGL_Caps.h"
#include "gl.h"
#include <assert.h>
#include <algorithm>
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

	enum SpecMinimum
	{
		GLSPEC_MAX_TEXTURE_BUFFER_SIZE_MIN = 65536,
	};

	void queryCapabilities()
	{
		GLint gl_maj = 0, gl_min = 0;

		m_supportFlags = 0;
		m_deviceTier = DEV_TIER_0;
		glGetIntegerv(GL_MAJOR_VERSION, &gl_maj);
		glGetIntegerv(GL_MINOR_VERSION, &gl_min);

		bool isMacOS = (strcmp(SDL_GetPlatform(), "Mac OS X") == 0);

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
		// On GLES, GL_OES_standard_derivatives is required for the GPU renderer.
		// Override device tier when it is available (the ARB extension checks above don't apply).
		if (SDL_GL_ExtensionSupported("GL_OES_standard_derivatives"))
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
