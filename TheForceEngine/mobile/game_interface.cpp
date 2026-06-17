#include "SDL.h"
#include "SDL_scancode.h"

// TFE headers MUST be included before game_interface.h.
// TFE_Input/inputEnum.h defines `enum MouseButton`, while
// Clibs_OpenTouch/game_interface.h declares `void MouseButton(int, int)` — same
// identifier, different kinds. C++ lets them coexist only if the type name is
// established first; otherwise the function declaration hides the enum and any
// later use of `MouseButton` as a type fails to compile.
#include <TFE_FrontEndUI/frontEndUi.h>
#include <TFE_DarkForces/darkForcesMain.h>
#include <TFE_DarkForces/GameUI/escapeMenu.h>
#include <TFE_DarkForces/GameUI/pda.h>
#include <TFE_DarkForces/GameUI/menu.h>
#include <TFE_Input/inputMapping.h>
#include <TFE_RenderBackend/renderBackend.h>
#include <TFE_Settings/settings.h>

#include "game_interface.h"

// SDL's internal keyboard injection — same trick iortcw / gzdoom use. Thread-safe:
// it pushes into the SDL event queue under SDL's internal lock. Used here only for
// menu navigation keys (UP/DOWN/LEFT/RIGHT/RETURN/ESC), which are hardcoded by both
// ImGui and the DOS-style DF menus and never user-remappable.
extern "C" int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode);

extern int main(int argc, char *argv[]);

// PortableAction stores button state into these arrays from the touch-UI thread.
// PortableTickActions(), called from the main loop each frame, then injects them
// into TFE_Input's per-action state table. We go straight to the action layer rather
// than synthesising SDL key events so the touch buttons keep working even if the
// user remaps the underlying keyboard binds.
static volatile uint8_t s_androidHeld   [TFE_Input::IA_COUNT] = { 0 };
static volatile uint8_t s_androidPressed[TFE_Input::IA_COUNT] = { 0 };

// Touch movement axes. Read out via PortableGetMove() once per frame from the
// player code hook in TFE_DarkForces/player.cpp.
static float s_androidFwd    = 0.0f;
static float s_androidStrafe = 0.0f;

// In-game-menu mouse style. true  = tap-to-position (teleport the cursor onto the
// tapped button; the touch layer hides the cursor), false = relative drag of a
// visible cursor. Toggled from the app via PortableSetMouseTapMode().
static bool s_mouseTapMode = true;

// iortcw-style look accumulators.
//  _mouse: per-swipe deltas — accumulate, drained & zeroed each frame.
//  _joy  : joystick magnitude — latest value held until next update; not zeroed.
static float s_lookPitchMouse = 0.0f, s_lookPitchJoy = 0.0f;
static float s_lookYawMouse   = 0.0f, s_lookYawJoy   = 0.0f;

// Look sensitivities. Input deltas from the touch layer are normalised; these
// scale them to pixel units that MouseMove() forwards to SDL_InjectMouse().
static const float ANDROID_LOOK_MOUSE_X_SCALE = 2500.0f;
static const float ANDROID_LOOK_MOUSE_Y_SCALE =  800.0f;
// Joystick-mode look is applied every frame while the stick is held, so the per-frame
// emit needs to be much smaller. Roughly matches iortcw's "look_yaw_joy * 6" at 60Hz.
static const float ANDROID_LOOK_JOY_X_SCALE   =   12.0f;
static const float ANDROID_LOOK_JOY_Y_SCALE   =    15.0f;

extern "C" {

void PortableInit(int argc, const char **argv)
{
    LOGI("PortableInit");
    main(argc, (char **)argv);
}

// Android system Back button → synthesize an ESC press + release. TFE's main loop
// will see both events from the SDL queue this frame; keyPressed(KEY_ESCAPE) latches
// on the down event so escape-menu / close-overlay logic fires normally.
void PortableBackButton(void)
{
    LOGI("PortableBackButton");
    SDL_SendKeyboardKey(SDL_PRESSED,  SDL_SCANCODE_ESCAPE);
    SDL_SendKeyboardKey(SDL_RELEASED, SDL_SCANCODE_ESCAPE);
}

// Hardware keyboard pass-through (physical USB / Bluetooth keyboard via the touch
// layer). `code` is already an SDL_Scancode — the touch layer translates Android
// KeyEvent codes before calling us. SDL injection is the right path here because
// these are real key events that the user expects to behave exactly like a desktop
// keyboard, including TFE's normal key→action remapping.
int PortableKeyEvent(int state, int code, int unitcode)
{
    SDL_SendKeyboardKey(state ? SDL_PRESSED : SDL_RELEASED, (SDL_Scancode)code);
    return 0;
}

// Map a PORT_ACT_* code to a TFE_Input InputAction. Returns IA_COUNT when no
// mapping exists (caller must skip).
static int actionForPortAct(int port_act)
{
    using namespace TFE_Input;
    switch (port_act)
    {
        // Movement.
        case PORT_ACT_FWD:        return IADF_FORWARD;
        case PORT_ACT_BACK:       return IADF_BACKWARD;
        case PORT_ACT_MOVE_LEFT:  return IADF_STRAFE_LT;
        case PORT_ACT_MOVE_RIGHT: return IADF_STRAFE_RT;
        case PORT_ACT_LEFT:       return IADF_TURN_LT;
        case PORT_ACT_RIGHT:      return IADF_TURN_RT;
        case PORT_ACT_LOOK_UP:    return IADF_LOOK_UP;
        case PORT_ACT_LOOK_DOWN:  return IADF_LOOK_DN;

        // Modifiers.
        case PORT_ACT_SPEED:      return IADF_RUN;
        case PORT_ACT_JUMP:
        case PORT_ACT_UP:         return IADF_JUMP;
        case PORT_ACT_CROUCH:
        case PORT_ACT_DOWN:       return IADF_CROUCH;

        // Interaction.
        case PORT_ACT_USE:        return IADF_USE;
        case PORT_ACT_ATTACK:     return IADF_PRIMARY_FIRE;
        case PORT_ACT_ALT_ATTACK: return IADF_SECONDARY_FIRE;

        // Weapon cycling.
        case PORT_ACT_NEXT_WEP:   return IADF_CYCLEWPN_NEXT;
        case PORT_ACT_PREV_WEP:   return IADF_CYCLEWPN_PREV;

        // DF screens.
        case PORT_ACT_MAP:        return IADF_AUTOMAP;
        case PORT_ACT_DATAPAD:    return IADF_PDA_TOGGLE;
        case PORT_ACT_CONSOLE:    return IAS_CONSOLE;

        // Save / load.
        case PORT_ACT_QUICKSAVE:  return IAS_QUICK_SAVE;
        case PORT_ACT_QUICKLOAD:  return IAS_QUICK_LOAD;

        // Weapon select. DF binds digits 1..9 → weapons 1..9 and 0 → weapon 10.
        case PORT_ACT_WEAP0:      return IADF_WEAPON_10;
        case PORT_ACT_WEAP1:      return IADF_WEAPON_1;
        case PORT_ACT_WEAP2:      return IADF_WEAPON_2;
        case PORT_ACT_WEAP3:      return IADF_WEAPON_3;
        case PORT_ACT_WEAP4:      return IADF_WEAPON_4;
        case PORT_ACT_WEAP5:      return IADF_WEAPON_5;
        case PORT_ACT_WEAP6:      return IADF_WEAPON_6;
        case PORT_ACT_WEAP7:      return IADF_WEAPON_7;
        case PORT_ACT_WEAP8:      return IADF_WEAPON_8;
        case PORT_ACT_WEAP9:      return IADF_WEAPON_9;

        default:                  return IA_COUNT;
    }
}

// Touch-thread: press = state non-zero, release = state zero. Same logic the
// gameplay action path uses — split out for reuse by the menu / weapon branches.
static void applyActionState(int state, int ia)
{
    if (state)
    {
        if (!s_androidHeld[ia])
        {
            s_androidPressed[ia] = 1;
        }
        s_androidHeld[ia] = 1;
    }
    else
    {
        s_androidHeld[ia] = 0;
    }
}

// Helper for menu-navigation key injection (non-remappable keys only — see header
// comment on SDL_SendKeyboardKey).
static void sendKey(int state, SDL_Scancode scancode)
{
    SDL_SendKeyboardKey(state ? SDL_PRESSED : SDL_RELEASED, scancode);
}

// Touch-UI thread entry point. Same branching shape as iortcw's PortableAction:
//   1. menu/blank → arrow keys / Enter / ESC via SDL injection; mouse buttons for
//      ImGui clicks. These keys aren't user-remappable so SDL events are safe.
//   2. gameplay → action injection (handled by actionForPortAct + applyActionState),
//      bypassing the keyboard layer so user remapping doesn't break the touch UI.
// Releases are never gated on screen mode so a held action can't get stuck if the
// screen flipped to TS_MENU between press and release.
void PortableAction(int state, int action)
{
    const touchscreemode_t mode = PortableGetScreenMode();
    const bool menuMode = (mode == TS_MENU || mode == TS_BLANK);

    // ---- Menu / blank screen: navigation via SDL keys, mouse via injection ----
    if (menuMode)
    {
        switch (action)
        {
            case PORT_ACT_MENU_UP:      sendKey(state, SDL_SCANCODE_UP);      return;
            case PORT_ACT_MENU_DOWN:    sendKey(state, SDL_SCANCODE_DOWN);    return;
            case PORT_ACT_MENU_LEFT:    sendKey(state, SDL_SCANCODE_LEFT);    return;
            case PORT_ACT_MENU_RIGHT:   sendKey(state, SDL_SCANCODE_RIGHT);   return;
            case PORT_ACT_MENU_SELECT:  sendKey(state, SDL_SCANCODE_RETURN);  return;
            case PORT_ACT_MENU_BACK:
            case PORT_ACT_MENU_ABORT:
            case PORT_ACT_MENU_SHOW:    sendKey(state, SDL_SCANCODE_ESCAPE);  return;
            case PORT_ACT_MENU_CONFIRM: sendKey(state, SDL_SCANCODE_Y);       return;

            case PORT_ACT_MOUSE_LEFT:   MouseButton(state, BUTTON_PRIMARY);   return;
            case PORT_ACT_MOUSE_RIGHT:  MouseButton(state, BUTTON_SECONDARY); return;
        }
        // Fall through: a release of a gameplay action that the player started
        // before opening a menu still needs to clear its held state (handled
        // below). Presses of gameplay actions while in menu also queue safely —
        // DF only reads them once we're back in mission mode.
    }

    // ---- Gameplay (and queued release) ----
    const int ia = actionForPortAct(action);
    if (ia >= TFE_Input::IA_COUNT) { return; }
    applyActionState(state, ia);
}

// Called once per frame from the main loop (TFE main.cpp) after the normal input
// pipeline has populated s_actions[] from real keyboard / mouse / controller state.
// We OR our touch state on top so the touch buttons take effect this frame.
//
// Per-frame because TFE_Input::inputMapping_endFrame() wipes s_actions[] to STATE_UP
// at end of every frame, then inputMapping_updateInput() refills it from live key
// state — our held actions have no live key behind them, so they need re-applying.
void PortableTickActions()
{
    using namespace TFE_Input;

    // Keep the DF menu cursor visibility in sync with the mouse style. Done here
    // (every frame, Android-only) so there's no startup window where the cursor
    // shows in tap mode, and so desktop — which never calls this — is unaffected.
    TFE_DarkForces::menu_setCursorHidden(s_mouseTapMode ? JTRUE : JFALSE);

    for (int i = 0; i < IA_COUNT; ++i)
    {
        if (s_androidPressed[i])
        {
            inputMapping_setStatePress((InputAction)i);
            s_androidPressed[i] = 0;
        }
        else if (s_androidHeld[i])
        {
            // Only fill the slot if no real input already claimed it this frame.
            // Avoids downgrading a real STATE_PRESSED to STATE_DOWN.
            if (inputMapping_getActionState((InputAction)i) == STATE_UP)
            {
                inputMapping_setStateDown((InputAction)i);
            }
        }
    }
}

static float clamp_unit(float v)
{
    if (v >  1.0f) return  1.0f;
    if (v < -1.0f) return -1.0f;
    return v;
}

void PortableMoveFwd(float fwd)
{
    s_androidFwd = clamp_unit(fwd);
}

void PortableMoveSide(float strafe)
{
    s_androidStrafe = clamp_unit(strafe);
}

void PortableMove(float fwd, float strafe)
{
    PortableMoveFwd(fwd);
    PortableMoveSide(strafe);
}

void PortableLookPitch(int mode, float pitch)
{
    switch (mode)
    {
        case LOOK_MODE_MOUSE:    s_lookPitchMouse += pitch; break;
        case LOOK_MODE_JOYSTICK: s_lookPitchJoy    = pitch; break;
    }
}

void PortableLookYaw(int mode, float yaw)
{
    switch (mode)
    {
        case LOOK_MODE_MOUSE:    s_lookYawMouse += yaw; break;
        case LOOK_MODE_JOYSTICK: s_lookYawJoy    = yaw; break;
    }
}

void PortableMouse(float dx, float dy)
{
    // A direct mouse/touch swipe is treated as mouse-mode look.
    s_lookYawMouse   += dx;
    s_lookPitchMouse += dy;
}

void PortableMouseAbs(float x, float y)
{
}

void PortableMouseButton(int state, int button, float dx, float dy)
{
}

// Select the in-game-menu mouse style (see s_mouseTapMode). The cursor-hidden
// state is pushed to the menu system every frame from PortableTickActions(), so
// just store the flag here.
void PortableSetMouseTapMode(int enable)
{
    s_mouseTapMode = (enable != 0);
}

int PortableGetMouseTapMode()
{
    return s_mouseTapMode ? 1 : 0;
}

void PortableCommand(const char *cmd)
{
}

void PortableAutomapControl(float zoom, float x, float y)
{
}

int PortableShowKeyboard(void)
{
    return 0;
}

bool PortableSetAlwaysRun(bool run)
{
    return false;
}

touchscreemode_t PortableGetScreenMode()
{
    if (TFE_FrontEndUI::isConsoleOpen())
    {
        return TS_CONSOLE;
    }

    const AppState appState = TFE_FrontEndUI::getAppState();
    switch (appState)
    {
        case APP_STATE_SET_DEFAULTS:
        case APP_STATE_NO_GAME_DATA:
        case APP_STATE_CANNOT_RUN:
        case APP_STATE_MENU:
            return TS_MENU;

        case APP_STATE_LOAD:
        case APP_STATE_EXIT_TO_MENU:
        case APP_STATE_QUIT:
        case APP_STATE_UNINIT:
            return TS_BLANK;

        case APP_STATE_GAME:
            break;

        default:
            return TS_BLANK;
    }

    if (TFE_FrontEndUI::isConfigMenuOpen())
    {
        return TS_MENU;
    }

    switch (TFE_DarkForces::darkforces_getSubState())
    {
        case TFE_DarkForces::DF_SUB_STARTUP_CUTSCENE:
        case TFE_DarkForces::DF_SUB_CUTSCENE:
            return TS_BLANK;

        case TFE_DarkForces::DF_SUB_AGENT_MENU:
        case TFE_DarkForces::DF_SUB_BRIEFING:
            return TS_MENU;

        case TFE_DarkForces::DF_SUB_MISSION:
            if (TFE_DarkForces::pda_isOpen())        { return TS_BLANK; }
            if (TFE_DarkForces::escapeMenu_isOpen()) { return TS_MENU; }
            return TS_GAME;
    }

    return TS_GAME;
}

// Horizontal pillarbox offset (in device pixels) of the active DF menu's drawn
// image. The engine maps the absolute mouse X assuming the menu is pinned to the
// left screen edge (see menu_handleMousePosition / escMenu_handleMousePosition),
// but the image is actually drawn centred, so the touch layer subtracts this so a
// tap lands under the finger. The Landru menus (agent menu, briefing, PDA) are
// always a 320x200 canvas shown at 4:3; the escape menu draws over the live game
// image, which fills the full width when widescreen is enabled (offset 0).
static float menuMouseOffsetX(bool escapeMenu)
{
    DisplayInfo info;
    TFE_RenderBackend::getDisplayInfo(&info);
    if (info.width <= info.height) { return 0.0f; }   // portrait fits to width

    if (escapeMenu && TFE_Settings::getGraphicsSettings()->widescreen)
    {
        return 0.0f;
    }

    const float displayedWidth = (float)info.height * 4.0f / 3.0f;
    if (displayedWidth >= (float)info.width) { return 0.0f; }
    return ((float)info.width - displayedWidth) * 0.5f;
}

// True only for the DarkForces-rendered in-mission menus (escape menu, agent
// menu, mission briefing, PDA). The ImGui frontend (main menu / config) reports
// false even though both surface as TS_MENU to the touch layer. Used to pick the
// on-screen mouse style: relative-drag for the small ImGui widgets, absolute
// tap-to-position for the large DOS-style menu buttons. Mirrors the state checks
// in PortableGetScreenMode(). When non-null, *mouseOffsetX receives the menu's
// horizontal pillarbox offset (device px) for the absolute-tap path.
int PortableInGameMenu(float* mouseOffsetX)
{
    if (mouseOffsetX) { *mouseOffsetX = 0.0f; }

    if (TFE_FrontEndUI::isConsoleOpen())                 { return 0; }
    if (TFE_FrontEndUI::getAppState() != APP_STATE_GAME) { return 0; }
    if (TFE_FrontEndUI::isConfigMenuOpen())              { return 0; }

    bool escapeMenu = false;
    switch (TFE_DarkForces::darkforces_getSubState())
    {
        case TFE_DarkForces::DF_SUB_AGENT_MENU:
        case TFE_DarkForces::DF_SUB_BRIEFING:
            break;

        case TFE_DarkForces::DF_SUB_MISSION:
            if (TFE_DarkForces::pda_isOpen())             { break; }
            if (TFE_DarkForces::escapeMenu_isOpen())      { escapeMenu = true; break; }
            return 0;

        default:
            return 0;
    }

    if (mouseOffsetX) { *mouseOffsetX = menuMouseOffsetX(escapeMenu); }
    return 1;
}

// True only while the ImGui frontend's top-level main menu (the large
// Start/Settings/... image buttons) is showing — not the config/mods/manual
// sub-screens, which keep their small widgets and stay on relative drag. The
// touch layer uses this to enable absolute tap-to-press on the big main-menu
// buttons, complementing PortableInGameMenu() for the DOS-style in-mission menus.
// No pillarbox offset is needed: ImGui draws in full window pixels, the same
// space the absolute mouse position uses.
int PortableFrontendMenu()
{
    if (TFE_FrontEndUI::isConsoleOpen()) { return 0; }
    return TFE_FrontEndUI::isMainMenuOpen() ? 1 : 0;
}



// Called from the player input path in TFE_DarkForces/player.cpp once per frame.
// Writes the latest touch-stick fwd/strafe (each in [-1, 1]) into the output args,
// and also drains the look accumulators by forwarding them to MouseMove() so DF picks
// the deltas up through the normal SDL mouse-motion path.
void PortableGetMove(float* fwd, float* strafe)
{
    if (fwd)    { *fwd    = s_androidFwd;    }
    if (strafe) { *strafe = s_androidStrafe; }

    const float yawPx   = s_lookYawMouse   * ANDROID_LOOK_MOUSE_X_SCALE
                        + s_lookYawJoy     * ANDROID_LOOK_JOY_X_SCALE;
    const float pitchPx = s_lookPitchMouse * ANDROID_LOOK_MOUSE_Y_SCALE
                        + -s_lookPitchJoy   * ANDROID_LOOK_JOY_Y_SCALE;

    if (yawPx != 0.0f || pitchPx != 0.0f)
    {
        MouseMove(yawPx, pitchPx);
    }

    // Mouse-mode is per-swipe; reset so we don't double-apply.
    // Joystick-mode is held; leave it until the touch layer pushes a new value.
    s_lookYawMouse   = 0.0f;
    s_lookPitchMouse = 0.0f;
}


} // extern "C"

