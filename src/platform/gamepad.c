#include "gamepad.h"

#define MAX_PADS 4
#define DEAD 12000

static SDL_GameController *pad[MAX_PADS];

static void open_free_slots(void)
{
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        int have = 0;

        if (!SDL_IsGameController(i))
            continue;
#ifdef __ANDROID__
        /* Android lists remotes and virtual devices as game controllers too: a real pad has sticks */
        {
            SDL_Joystick *j = SDL_JoystickOpen(i);
            int axes = j ? SDL_JoystickNumAxes(j) : 0;
            SDL_Log("WWF: joystick %d: %s (%d axes, %d buttons)", i, SDL_JoystickNameForIndex(i), axes,
                    j ? SDL_JoystickNumButtons(j) : 0);
            if (j)
                SDL_JoystickClose(j);
            if (axes < 2)
                continue;
        }
#endif
        for (int s = 0; s < MAX_PADS; s++)
            if (pad[s] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad[s])) == SDL_JoystickGetDeviceInstanceID(i))
                have = 1;
        if (have)
            continue;
        for (int s = 0; s < MAX_PADS; s++)
            if (!pad[s]) {
                pad[s] = SDL_GameControllerOpen(i);
#ifdef __ANDROID__
                SDL_Log("WWF: controller %d in slot %d: %s", i, s, pad[s] ? SDL_GameControllerName(pad[s]) : SDL_GetError());
#endif
                break;
            }
    }
}

void gamepad_open(void)
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        return;
    open_free_slots();
#ifdef __ANDROID__
    SDL_Log("WWF: %d joystick(s), game controllers opened above", SDL_NumJoysticks());
#endif
}

void gamepad_event(const SDL_Event *e)
{
    if (e->type == SDL_CONTROLLERDEVICEADDED) {
        open_free_slots();
    } else if (e->type == SDL_CONTROLLERDEVICEREMOVED) {
        for (int s = 0; s < MAX_PADS; s++)
            if (pad[s] && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad[s])) == e->cdevice.which) {
                SDL_GameControllerClose(pad[s]);
                pad[s] = NULL;
            }
    }
}

void gamepad_close(void)
{
    for (int s = 0; s < MAX_PADS; s++)
        if (pad[s]) {
            SDL_GameControllerClose(pad[s]);
            pad[s] = NULL;
        }
}

#define AXIS(a) SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_##a)

void gamepad_read(wolf *w, const int *btn)
{
    for (int s = 0; s < MAX_PADS; s++) {
        SDL_GameController *c = pad[s];
        int down[11];

        if (!c)
            continue;
        for (int a = 0; a < 11; a++)
            down[a] = btn[a] >= 0 && SDL_GameControllerGetButton(c, (SDL_GameControllerButton)btn[a]);
        /* the order is settings.h ACT_UP.. ACT_RUN */
        if (s >= 2) {                     /* players 3 and 4: for mods (wolf.extra_player) */
            unsigned v = 0;

            if (down[0] || AXIS(LEFTY) < -DEAD) v |= WOLF_UP;
            if (down[1] || AXIS(LEFTY) > DEAD) v |= WOLF_DOWN;
            if (down[2] || AXIS(LEFTX) < -DEAD) v |= WOLF_LEFT;
            if (down[3] || AXIS(LEFTX) > DEAD) v |= WOLF_RIGHT;
            if (down[10] || down[4]) v |= WOLF_X_PUNCH;
            if (down[5]) v |= WOLF_X_BLOCK;
            if (down[6]) v |= WOLF_X_SPUNCH;
            if (down[10] || down[7]) v |= WOLF_X_KICK;
            if (down[8]) v |= WOLF_X_SKICK;
            if (down[9]) v |= WOLF_X_START;
            w->extra_player[s - 2] |= (uint16_t)v;
            continue;
        }
        if (down[0] || AXIS(LEFTY) < -DEAD) w->player[s] |= WOLF_UP;
        if (down[1] || AXIS(LEFTY) > DEAD) w->player[s] |= WOLF_DOWN;
        if (down[2] || AXIS(LEFTX) < -DEAD) w->player[s] |= WOLF_LEFT;
        if (down[3] || AXIS(LEFTX) > DEAD) w->player[s] |= WOLF_RIGHT;
        if (down[10] || down[4]) w->player[s] |= WOLF_B1;
        if (down[5]) w->player[s] |= WOLF_B2;
        if (down[6]) w->player[s] |= WOLF_B3;
        if (down[10] || down[7]) w->player[2] |= (uint8_t)(1 << (4 * s));
        if (down[8]) w->player[2] |= (uint8_t)(2 << (4 * s));
        if (down[9]) w->coin_bits |= s ? WOLF_START2 : WOLF_START1;
        if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK))
            w->coin_bits |= s ? WOLF_COIN2 : WOLF_COIN1;
    }
}
