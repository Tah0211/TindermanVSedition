#ifndef SCENE_BATTLE_H
#define SCENE_BATTLE_H

#include <SDL2/SDL.h>

void scene_battle_enter(void);
void scene_battle_leave(void);
void scene_battle_update(float dt);
void scene_battle_render(SDL_Renderer *r);

#endif
