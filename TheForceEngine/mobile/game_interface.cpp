#include "SDL.h"

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
#include <TFE_Input/inputMapping.h>

#include "game_interface.h"

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

// iortcw-style look accumulators.
//  _mouse: per-swipe deltas — accumulate, drained & zeroed each frame.
//  _joy  : joystick magnitude — latest value held until next update; not zeroed.
static float s_lookPitchMouse = 0.0f, s_lookPitchJoy = 0.0f;
static float s_lookYawMouse   = 0.0f, s_lookYawJoy   = 0.0f;

// Look sensitivities. Input deltas from the touch layer are normalised; these
// scale them to pixel units that MouseMove() forwards to SDL_InjectMouse().
static const float ANDROID_LOOK_MOUSE_X_SCALE = 1000.0f;
static const float ANDROID_LOOK_MOUSE_Y_SCALE =  800.0f;
// Joystick-mode look is applied every frame while the stick is held, so the per-frame
// emit needs to be much smaller. Roughly matches iortcw's "look_yaw_joy * 6" at 60Hz.
static const float ANDROID_LOOK_JOY_X_SCALE   =   12.0f;
static const float ANDROID_LOOK_JOY_Y_SCALE   =    8.0f;

extern "C" {

void PortableInit(int argc, const char **argv)
{
    LOGI("PortableInit");
    main(argc, (char **)argv);
}

void PortableBackButton(void)
{
}

int PortableKeyEvent(int state, int code, int unitcode)
{
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

        default:                  return IA_COUNT;
    }
}

// Called on press (state == 1) and release (state == 0) from the touch-UI thread.
// We don't gate on PortableGetScreenMode() — a release that arrives after the screen
// flipped to TS_MENU still needs to clear our held state, otherwise the action would
// stay stuck once we returned to gameplay.
void PortableAction(int state, int action)
{
    const int ia = actionForPortAct(action);
    if (ia >= TFE_Input::IA_COUNT) { return; }

    if (state)
    {
        if (!s_androidHeld[ia])
        {
            s_androidPressed[ia] = 1;   // one-shot for "press transition" semantics
        }
        s_androidHeld[ia] = 1;
    }
    else
    {
        s_androidHeld[ia] = 0;
    }
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
            return TS_MENU;

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
                        + s_lookPitchJoy   * ANDROID_LOOK_JOY_Y_SCALE;

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

