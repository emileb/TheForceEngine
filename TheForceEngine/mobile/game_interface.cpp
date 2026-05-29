#include "SDL.h"
#include "game_interface.h"

#include <TFE_FrontEndUI/frontEndUi.h>
#include <TFE_DarkForces/darkForcesMain.h>
#include <TFE_DarkForces/GameUI/escapeMenu.h>
#include <TFE_DarkForces/GameUI/pda.h>

extern int main(int argc, char *argv[]);

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

void PortableMoveFwd(float fwd)
{
}

void PortableMoveSide(float strafe)
{
}

void PortableMove(float fwd, float strafe)
{
}

void PortableLookPitch(int mode, float pitch)
{
}

void PortableLookYaw(int mode, float yaw)
{
}

void PortableMouse(float dx, float dy)
{
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

} // extern "C"

