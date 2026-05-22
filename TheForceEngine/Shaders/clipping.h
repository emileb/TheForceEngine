// GLES-friendly portal clip plane abstraction.
//
// On desktop GL (and on GLES when GL_EXT_clip_cull_distance is enabled), map
// Frag_ClipDistance onto the built-in gl_ClipDistance so the hardware does the
// clipping. Otherwise fall back to writing a regular varying array and discarding
// out-of-plane fragments by hand. GL_ES is predefined as 1 by all GLES compilers
// and absent on desktop, so we use it to gate the fallback.
#if defined(VERTEX_SHADER)
	#if !defined(GL_ES) || defined(GL_EXT_clip_cull_distance)
		#define Frag_ClipDistance gl_ClipDistance
	#else
		out float Frag_ClipDistance[8];
	#endif
#elif defined(FRAGMENT_SHADER)
	#if !defined(GL_ES) || defined(GL_EXT_clip_cull_distance)
		void Clip() {}
	#else
		in float Frag_ClipDistance[8];
		void Clip()
		{
			for (int i = 0; i < 8; i++)
			{
				if (Frag_ClipDistance[i] < 0.0)
				{
					discard;
				}
			}
		}
	#endif
#endif
