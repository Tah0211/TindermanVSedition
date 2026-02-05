#ifndef SCENE_ALLOCATE_H
#define SCENE_ALLOCATE_H

#include <SDL2/SDL.h>

void scene_allocate_enter(void);
void scene_allocate_leave(void);
void scene_allocate_update(float dt);
void scene_allocate_render(SDL_Renderer *r);

#endif
