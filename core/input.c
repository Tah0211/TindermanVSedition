// core/input.c
#include "input.h"
#include "../core/engine.h" // g_running
#include <string.h>
#include <SDL2/SDL.h>

static Uint8 prev_keys[SDL_NUM_SCANCODES];
static Uint8 curr_keys[SDL_NUM_SCANCODES];

static char g_text_input_buffer[128];

static void clear_key_states_internal(void)
{
    memset(prev_keys, 0, sizeof(prev_keys));
    memset(curr_keys, 0, sizeof(curr_keys));
}

void input_clear(void)
{
    clear_key_states_internal();
    g_text_input_buffer[0] = '\0';
}

void input_begin_frame(void)
{
    memcpy(prev_keys, curr_keys, SDL_NUM_SCANCODES);
    g_text_input_buffer[0] = '\0';
}

void input_handle_event(const SDL_Event *e)
{
    if (!e) return;

    switch (e->type)
    {
    case SDL_QUIT:
        g_running = false;
        break;

    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            // 押しっぱなし・取りこぼし防止
            input_clear();
        }
        break;

    case SDL_TEXTINPUT:
        strncpy(g_text_input_buffer, e->text.text, sizeof(g_text_input_buffer) - 1);
        g_text_input_buffer[sizeof(g_text_input_buffer) - 1] = '\0';
        break;

    case SDL_TEXTEDITING:
        // 未確定文字が必要ならここで扱う
        break;

    default:
        break;
    }
}

void input_end_frame(void)
{
    // state 更新（main 側で PollEvent を回しているので、ここで十分）
    SDL_PumpEvents();

    int num = 0;
    const Uint8 *state = SDL_GetKeyboardState(&num);
    if (num > SDL_NUM_SCANCODES) num = SDL_NUM_SCANCODES;
    memcpy(curr_keys, state, num);

    // グローバル仕様：ESCで強制終了（押下1回）
    if (curr_keys[SDL_SCANCODE_ESCAPE] && !prev_keys[SDL_SCANCODE_ESCAPE]) {
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

const char *input_get_text(void)
{
    return (g_text_input_buffer[0] != '\0') ? g_text_input_buffer : NULL;
}
