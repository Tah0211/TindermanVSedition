#ifndef SCENE_SELECT_H
#define SCENE_SELECT_H

#include <SDL2/SDL.h>

void scene_select_enter(void);
void scene_select_update(float dt);
void scene_select_render(SDL_Renderer *r); // ← ★ render に変更、引数追加
void scene_select_leave(void);             // ← ★ exit → leave に変更

#endif
