#include "input.h"
#include "../core/engine.h" // ★ g_running を使うため必要
#include <string.h>

// ★ これらの定義が必須！
static Uint8 prev_keys[SDL_NUM_SCANCODES];
static Uint8 curr_keys[SDL_NUM_SCANCODES];

void input_update(void)
{
    memcpy(prev_keys, curr_keys, SDL_NUM_SCANCODES);

    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        if (e.type == SDL_QUIT)
        {
            g_running = false;
        }
    }

    int num;
    const Uint8 *state = SDL_GetKeyboardState(&num);
    if (num > SDL_NUM_SCANCODES)
        num = SDL_NUM_SCANCODES;
    memcpy(curr_keys, state, num);

    // ★ ESC でゲーム終了
    if (curr_keys[SDL_SCANCODE_ESCAPE])
    {
        g_running = false;
    }
}

bool input_is_pressed(SDL_Scancode sc)
{
    return curr_keys[sc] && !prev_keys[sc];
}

bool input_is_down(SDL_Scancode sc)
{
    return curr_keys[sc];
}

bool input_is_released(SDL_Scancode sc)
{
    return !curr_keys[sc] && prev_keys[sc];
}
