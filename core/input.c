// core/input.c
#include "input.h"
#include "../core/engine.h" // g_running
#include <string.h>
#include <SDL2/SDL.h>

// ======================================================
//  キーボード状態
// ======================================================
static Uint8 prev_keys[SDL_NUM_SCANCODES];
static Uint8 curr_keys[SDL_NUM_SCANCODES];

// ======================================================
//  SDL_TEXTINPUT / IME入力バッファ（1フレーム）
// ======================================================
static char g_text_input_buffer[128];

// ======================================================
//  input_update
// ======================================================
void input_update(void)
{
    // 前フレームのキー状態を保存
    memcpy(prev_keys, curr_keys, SDL_NUM_SCANCODES);

    // 今フレームの文字入力リセット
    g_text_input_buffer[0] = '\0';

    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        switch (e.type)
        {
        case SDL_QUIT:
            g_running = false;
            break;

        case SDL_TEXTINPUT:
            // IME 確定文字（UTF-8）
            strncpy(g_text_input_buffer,
                    e.text.text,
                    sizeof(g_text_input_buffer) - 1);
            g_text_input_buffer[sizeof(g_text_input_buffer) - 1] = '\0';
            break;

        case SDL_TEXTEDITING:
            // 未確定文字（必要になったら使う）
            break;

        default:
            break;
        }
    }

    // キーボード状態の更新
    int num = 0;
    const Uint8 *state = SDL_GetKeyboardState(&num);
    if (num > SDL_NUM_SCANCODES)
        num = SDL_NUM_SCANCODES;
    memcpy(curr_keys, state, num);

    // デバッグ：ESCで終了
    if (curr_keys[SDL_SCANCODE_ESCAPE])
        g_running = false;
}

// ======================================================
//  キー入力判定
// ======================================================
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

// ======================================================
//  CHAT：確定した文字を1フレーム分返す
// ======================================================
const char *input_get_text(void)
{
    return (g_text_input_buffer[0] != '\0') ? g_text_input_buffer : NULL;
}
