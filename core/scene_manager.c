// core/scene_manager.c

#include "scene_manager.h"

#include "../scenes/1_scene_home.h"
#include "../scenes/2_scene_select.h"
#include "../scenes/3_scene_chat.h"
#include "../scenes/4_scene_allocate.h"
#include "../scenes/5_scene_battle.h"   // ★追加：バトル

#include <stdio.h>

static SceneID current_scene;
static SceneID g_last_render_scene = (SceneID)-1; // ★renderログ用

// -------------------------------------------------------------
// ★追加：遷移前に現在シーンの leave を呼ぶ
//  - 各シーンが leave を持っている前提。
//  - もし未実装のシーンがあるなら、呼び出しをコメントアウトするか
//    空関数を用意してください。
// -------------------------------------------------------------
static void call_leave(SceneID s)
{
    switch (s)
    {
    case SCENE_HOME:
        // home が leave を持つならここで呼ぶ（無ければ何もしない）
        // scene_home_leave();
        break;

    case SCENE_SELECT:
        // select が exit/leave を持つならここで呼ぶ（無ければ何もしない）
        // scene_select_exit();
        break;

    case SCENE_CHAT:
        // chat が exit/leave を持つならここで呼ぶ（無ければ何もしない）
        // scene_chat_exit();
        break;

    case SCENE_ALLOCATE:
        scene_allocate_leave();
        break;

    case SCENE_BATTLE:
        scene_battle_leave();
        break;

    default:
        break;
    }
}

void scene_manager_init(void)
{
    current_scene = SCENE_HOME;
    printf("[SCENE] init -> HOME (%d)\n", (int)current_scene);

    scene_home_enter();
}

void change_scene(SceneID next)
{
    printf("[SCENE] change_scene: %d -> %d\n", (int)current_scene, (int)next);

    // ★追加：遷移前に前シーンの leave
    call_leave(current_scene);

    current_scene = next;

    switch (next)
    {
    case SCENE_HOME:
        printf("[SCENE] enter HOME\n");
        scene_home_enter();
        break;

    case SCENE_SELECT:
        printf("[SCENE] enter SELECT\n");
        scene_select_enter();
        break;

    case SCENE_CHAT:
        printf("[SCENE] enter CHAT\n");
        scene_chat_enter();
        break;

    case SCENE_ALLOCATE:
        printf("[SCENE] enter ALLOCATE\n");
        scene_allocate_enter();
        break;

    case SCENE_BATTLE:
        printf("[SCENE] enter BATTLE\n");
        scene_battle_enter();
        break;

    default:
        printf("[SCENE] enter UNKNOWN (%d)\n", (int)next);
        break;
    }
}

void scene_update(float dt)
{
    switch (current_scene)
    {
    case SCENE_HOME:
        scene_home_update(dt);
        break;

    case SCENE_SELECT:
        scene_select_update(dt);
        break;

    case SCENE_CHAT:
        scene_chat_update(dt);
        break;

    case SCENE_ALLOCATE:
        scene_allocate_update(dt);
        break;

    case SCENE_BATTLE:
        scene_battle_update(dt);
        break;

    default:
        break;
    }
}

void scene_render(SDL_Renderer *r)
{
    // ★シーンが変わった瞬間だけ renderログを出す（毎フレームはうるさい）
    if (current_scene != g_last_render_scene)
    {
        printf("[SCENE] render now: %d\n", (int)current_scene);
        g_last_render_scene = current_scene;
    }

    switch (current_scene)
    {
    case SCENE_HOME:
        scene_home_render(r);
        break;

    case SCENE_SELECT:
        scene_select_render(r);
        break;

    case SCENE_CHAT:
        scene_chat_render(r);
        break;

    case SCENE_ALLOCATE:
        scene_allocate_render(r);
        break;

    case SCENE_BATTLE:
        scene_battle_render(r);
        break;

    default:
        break;
    }
}
