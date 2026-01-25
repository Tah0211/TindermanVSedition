#ifndef SCENE_MANAGER_H
#define SCENE_MANAGER_H

#include <SDL2/SDL.h>

typedef enum
{
    SCENE_HOME = 0,
    SCENE_SELECT,
    SCENE_CHAT,
    SCENE_ALLOCATE,
    SCENE_BATTLE,   // ★追加
} SceneID;


void scene_manager_init(void);
void change_scene(SceneID next);
void scene_update(float dt);
void scene_render(SDL_Renderer* r);

#endif
