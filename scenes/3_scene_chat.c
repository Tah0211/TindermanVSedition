#include "3_scene_chat.h"
#include "../core/input.h"
#include "../core/scene_manager.h"
#include <SDL2/SDL.h>

void scene_chat_enter(void)
{
    // 後でチャット用の初期化を書く
}

void scene_chat_update(float dt)
{
    (void)dt;
    // Backspace で HOME に戻す
    if (input_is_pressed(SDL_SCANCODE_BACKSPACE)) {
        change_scene(SCENE_HOME);
    }
}

void scene_chat_render(SDL_Renderer* r)
{
    SDL_SetRenderDrawColor(r, 10, 40, 10, 255);
    SDL_RenderClear(r);
}
