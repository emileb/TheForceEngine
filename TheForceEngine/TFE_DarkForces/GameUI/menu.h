#pragma once
//////////////////////////////////////////////////////////////////////
// General Dark Forces shared menu code.
//////////////////////////////////////////////////////////////////////

#include <TFE_System/types.h>
#include <TFE_DarkForces/Landru/lrect.h>

namespace TFE_DarkForces
{
	void menu_init();
	void menu_destroy();
	void menu_resetState();

	void menu_handleMousePosition();
	void menu_blitCursor(s32 x, s32 y, u8* framebuffer);
	void menu_resetCursor();
	u8*  menu_startupDisplay();

#ifdef __ANDROID__
	// When hidden, the menu mouse cursor is not drawn (the mouse position is still
	// tracked for hit-testing). Used by the Android touch UI's tap-to-position mode,
	// where the finger replaces the cursor. Defaults to visible.
	void  menu_setCursorHidden(JBool hidden);
	JBool menu_getCursorHidden();
#endif

	JBool menu_openResourceArchive(const char* name);
	void  menu_closeResourceArchive();

	// 'framebuffer' can be passed in to override the default source bitmap.
	// If null, the Landru bitmap will be used.
	void menu_blitToScreen(u8* framebuffer = nullptr, JBool transparent = JFALSE, JBool swap = JTRUE);
	void menu_blitCursorScaled(s16 x, s16 y, u8* buffer);
	   
	// Variables.
	extern Vec2i s_cursorPos;
	extern s32 s_buttonPressed;
	extern JBool s_buttonHover;
}
