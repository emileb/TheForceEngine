#pragma once
//////////////////////////////////////////////////////////////////////
// A central place to store OpenGL capabilities for the current device.
//////////////////////////////////////////////////////////////////////

#include <TFE_System/types.h>

enum DeviceTier
{
	DEV_TIER_0 = 0,	// Cannot support Gpu blit, use GDI.
	DEV_TIER_1,		// Can support Gpu blit.
	DEV_TIER_2,		// Can support Gpu renderer & Gpu color correction.
	DEV_TIER_3,		// Can support Gpu Compute renderer.
};

namespace OpenGL_Caps
{
	// Force the renderer down to a specific GLES feature level (set from the command line before
	// context creation / capability query). 0 = auto-detect the highest capable mode (default),
	// 20 = force the GLES 2.0 fallback (software renderer + GPU blit only), 30 = force the GLES 3.0
	// path (2D-texture buffer emulation, no native texture buffers). Has no effect on desktop GL.
	void setForceGLESVersion(s32 version);
	s32 getForceGLESVersion();

	void queryCapabilities();

	bool supportsPbo();
	bool supportsVbo();
	bool supportsFbo();
	bool supportsNonPow2Textures();
	bool supportsTextureArrays();
	bool supportsAniso();
	bool supportsClipping();
	bool supportsNoPerspectiveInterpolation();
	bool supportsTextureBuffer();
	// True when the active context is OpenGL ES 2.0 (GLSL ES 1.00). In this mode only the
	// software renderer + blit path is supported (no GPU renderer / color conversion).
	bool isGLES2();

	bool deviceSupportsGpuBlit();
	bool deviceSupportsGpuColorConversion();
	bool deviceSupportsGpuRenderer();

	u32 getDeviceTier();
	s32 getMaxTextureBufferSize();
	f32 getMaxAnisotropy();
	f32 getAnisotropyFromQuality(f32 quality);
};
