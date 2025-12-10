// scenes/3_scene_chat.h
#pragma once
#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // シーンの基本4関数
    void scene_chat_enter(void);
    void scene_chat_update(float dt);
    void scene_chat_render(SDL_Renderer *r);
    void scene_chat_leave(void);

#ifdef __cplusplus
}
#endif
