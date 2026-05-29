#include "SDL.h"
#include "game_interface.h"

#include <TFE_FrontEndUI/frontEndUi.h>
#include <TFE_DarkForces/darkForcesMain.h>
#include <TFE_DarkForces/GameUI/escapeMenu.h>
#include <TFE_DarkForces/GameUI/pda.h>

extern int main(int argc, char *argv[]);

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

void PortableAction(int state, int action)
{
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

