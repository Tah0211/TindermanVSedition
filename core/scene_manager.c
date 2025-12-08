#include "scene_manager.h"
#include "../scenes/1_scene_home.h"
#include "../scenes/2_scene_select.h"
#include "../scenes/3_scene_chat.h"

static SceneID current_scene;

void scene_manager_init(void)
{
    current_scene = SCENE_HOME;
    scene_home_enter();
}

void change_scene(SceneID next)
{
    current_scene = next;

    switch (next) {
    case SCENE_HOME:   scene_home_enter();   break;
    case SCENE_SELECT: scene_select_enter(); break;
    case SCENE_CHAT:   scene_chat_enter();   break;
    }
}

void scene_update(float dt)
{
    switch (current_scene) {
    case SCENE_HOME:   scene_home_update(dt);   break;
    case SCENE_SELECT: scene_select_update(dt); break;
    case SCENE_CHAT:   scene_chat_update(dt);   break;
    }
}

void scene_render(SDL_Renderer* r)
{
    switch (current_scene) {
    case SCENE_HOME:   scene_home_render(r);   break;
    case SCENE_SELECT: scene_select_render(r); break;
    case SCENE_CHAT:   scene_chat_render(r);   break;
    }
}
