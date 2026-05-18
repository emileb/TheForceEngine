#include "SDL.h"
#include "game_interface.h"

extern int main(int argc, char *argv[]);

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
    return TS_GAME;
}

