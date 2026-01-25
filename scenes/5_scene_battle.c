#include "5_scene_battle.h"
#include "../core/engine.h"
#include "../core/input.h"
#include "../core/scene_manager.h"
#include "../ui/ui_text.h"
#include "../util/json.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdbool.h>

static TTF_Font *g_font = NULL;

static int g_hp = 0, g_atk = 0, g_spd = 0, g_st = 0;
static int g_enemy_hp = 100;

static int read_int_or(const char *key, int fallback)
{
    int v = fallback;
    json_read_int("build.json", key, &v);
    return v;
}

void scene_battle_enter(void)
{
    printf("[BATTLE] enter\n");
    if (!g_font) g_font = ui_load_font("assets/font/main.otf", 36);

    int hp_base  = read_int_or("hp_base", 0);
    int atk_base = read_int_or("atk_base", 0);
    int sp_base  = read_int_or("sp_base", 0);
    int st_base  = read_int_or("st_base", 0);

    int hp_add  = read_int_or("hp_add", 0);
    int atk_add = read_int_or("atk_add", 0);
    int sp_add  = read_int_or("sp_add", 0);
    int st_add  = read_int_or("st_add", 0);

    g_hp  = hp_base  + hp_add;
    g_atk = atk_base + atk_add;
    g_spd = sp_base  + sp_add;
    g_st  = st_base  + st_add;

    g_enemy_hp = 100;

    printf("[BATTLE] stats: HP=%d ATK=%d SPD=%d ST=%d\n", g_hp, g_atk, g_spd, g_st);
}

void scene_battle_leave(void)
{
    // 使い回しなら閉じないでもOK。閉じたいならここで。
    // if (g_font) { TTF_CloseFont(g_font); g_font = NULL; }
}

void scene_battle_update(float dt)
{
    (void)dt;

    // 仮：Enterで敵に攻撃、EscでHOMEへ
    if (input_is_pressed(SDL_SCANCODE_RETURN))
    {
        int dmg = (g_atk > 0) ? g_atk : 1;
        g_enemy_hp -= dmg;
        if (g_enemy_hp < 0) g_enemy_hp = 0;
    }

    if (input_is_pressed(SDL_SCANCODE_ESCAPE))
    {
        change_scene(SCENE_HOME);
        return;
    }
}

void scene_battle_render(SDL_Renderer *r)
{
    SDL_SetRenderDrawColor(r, 10, 10, 16, 255);
    SDL_RenderClear(r);

    char buf[256];
    snprintf(buf, sizeof(buf), "BATTLE (prototype)");
    ui_text_draw(r, g_font, buf, 60, 40);

    snprintf(buf, sizeof(buf), "YOU  HP:%d  ATK:%d  SPD:%d  ST:%d", g_hp, g_atk, g_spd, g_st);
    ui_text_draw(r, g_font, buf, 60, 130);

    snprintf(buf, sizeof(buf), "ENEMY HP:%d", g_enemy_hp);
    ui_text_draw(r, g_font, buf, 60, 200);

    ui_text_draw(r, g_font, "Enter: Attack   Esc: Back to HOME", 60, 620);
}
