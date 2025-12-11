#include "2_scene_select.h"
#include "../core/scene_manager.h"
#include "../core/input.h"
#include "../core/engine.h"
#include "../util/texture.h"
#include "../ui/ui_text.h"
#include "../util/json.h" // build.json utility

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h> // ← 追加：build.json 初期化用

// ==============================================================
// キャラデータ構造体
// ==============================================================
typedef struct
{
    const char *id;
    const char *name;
    const char *type;
    const char *portrait_path;
    const char *voice_path;
} Girl;

// ==============================================================
// キャラデータ
// ==============================================================
static Girl girls[3] = {
    {"himari", "冥鳴ひまり", "近接",
     "assets/girls/himari/portrait.png",
     "assets/voice/himari/select.wav"},

    {"kiritan", "東北きりたん", "遠距離",
     "assets/girls/kiritan/portrait.png",
     "assets/voice/kiritan/select.wav"},

    {"sayo", "小夜", "サポート",
     "assets/girls/sayo/portrait.png",
     "assets/voice/sayo/select.wav"},
};

// ==============================================================
// ステート
// ==============================================================
static SDL_Texture *tex_portrait[3];
static SDL_Texture *tex_selected_icon;
static SDL_Texture *tex_spark;

static TTF_Font *font_main;
static TTF_Font *font_timer;

static int focus = 0;
static int decided_index = -1;

static bool finalizing = false;
static bool pending_finalize = false;

static Uint32 start_ms;
static Uint32 deadline_ms;
static Uint32 finalize_start_ms;

static Mix_Chunk *voice_girl[3] = {NULL};

static float glow_t = 0.0f;

// ==============================================================
// build.json 初期化（★ここで完全に書き直す）
// ==============================================================
static void reset_build_json(void)
{
    const char *path = "build.json";
    FILE *fp = fopen(path, "w");
    if (!fp)
    {
        SDL_Log("[SELECT] Failed to open %s for write", path);
        return;
    }

    // 初期好感度 30 / stats={}, skills=[] をまとめて出力
    fputs("{\n", fp);
    fputs("  \"girl_id\": \"\",\n", fp);
    fputs("  \"affection\": 30,\n", fp);
    fputs("  \"stats\": {},\n", fp);
    fputs("  \"skills\": []\n", fp);
    fputs("}\n", fp);

    fclose(fp);
    SDL_Log("[SELECT] build.json reset with initial affection=30");
}

// ==============================================================
// 色補間
// ==============================================================
static SDL_Color lerp_color(SDL_Color a, SDL_Color b, float t)
{
    SDL_Color c;
    c.r = a.r + (b.r - a.r) * t;
    c.g = a.g + (b.g - a.g) * t;
    c.b = a.b + (b.b - a.b) * t;
    c.a = 255;
    return c;
}

// ==============================================================
// 枠の周回位置
// ==============================================================
static void get_spark_pos(SDL_Rect dst, float t, float *ox, float *oy)
{
    float x = dst.x, y = dst.y, w = dst.w, h = dst.h;

    t = fmodf(t, 1.0f);

    if (t < 0.25f)
    {
        float p = t / 0.25f;
        *ox = x + w * p;
        *oy = y;
    }
    else if (t < 0.50f)
    {
        float p = (t - 0.25f) / 0.25f;
        *ox = x + w;
        *oy = y + h * p;
    }
    else if (t < 0.75f)
    {
        float p = (t - 0.50f) / 0.25f;
        *ox = x + w * (1.0f - p);
        *oy = y + h;
    }
    else
    {
        float p = (t - 0.75f) / 0.25f;
        *ox = x;
        *oy = y + h * (1.0f - p);
    }
}

// ==============================================================
// ネオン枠（ブラー付）
// ==============================================================
static void draw_neon_frame(SDL_Renderer *R, SDL_Rect dst, float t)
{
    SDL_Color pink = {255, 80, 180, 255};
    SDL_Color blue = {80, 160, 255, 255};

    int x = dst.x, y = dst.y, w = dst.w, h = dst.h;

    // 上下ライン
    for (int i = 0; i < w; i++)
    {
        float p = (float)i / (float)(w - 1);
        SDL_Color col = lerp_color(pink, blue, p);
        SDL_SetRenderDrawColor(R, col.r, col.g, col.b, 255);

        SDL_RenderDrawPoint(R, x + i, y);
        SDL_RenderDrawPoint(R, x + i, y + h);
    }

    // 左右ライン
    SDL_SetRenderDrawColor(R, pink.r, pink.g, pink.b, 255);
    SDL_RenderDrawLine(R, x, y, x, y + h);

    SDL_SetRenderDrawColor(R, blue.r, blue.g, blue.b, 255);
    SDL_RenderDrawLine(R, x + w, y, x + w, y + h);

    // スパーク（尾引き）
    if (tex_spark)
    {
        const int trail_count = 12;
        const float trail_gap = 0.022f;
        const float size_base = 42.0f;

        for (int i = 0; i < trail_count; i++)
        {
            float tt = t - trail_gap * i;
            if (tt < 0)
                tt += 1.0f;
            if (tt > 1)
                tt -= 1.0f;

            float cx, cy;
            get_spark_pos(dst, tt, &cx, &cy);

            float intensity = 1.0f - ((float)i / trail_count);

            SDL_Color col = lerp_color(pink, blue, tt);
            Uint8 r = col.r * (0.6f + 0.4f * intensity);
            Uint8 g = col.g * (0.6f + 0.4f * intensity);
            Uint8 b = col.b * (0.6f + 0.4f * intensity);

            SDL_SetTextureColorMod(tex_spark, r, g, b);
            SDL_SetTextureAlphaMod(tex_spark, (Uint8)(255 * powf(intensity, 1.8f)));

            float size = size_base * (0.55f + 0.45f * intensity);
            SDL_Rect sp = {(int)(cx - size / 2), (int)(cy - size / 2), (int)size, (int)size};

            SDL_RenderCopy(R, tex_spark, NULL, &sp);
        }

        SDL_SetTextureAlphaMod(tex_spark, 255);
    }
}

// ==============================================================
// finalize_selection
// ==============================================================
static void finalize_selection(void)
{
    if (finalizing)
        return;
    finalizing = true;

    int final_choice = (decided_index != -1) ? decided_index : focus;
    if (final_choice < 0 || final_choice >= 3)
        final_choice = 0;

    decided_index = final_choice;
    const char *gid = girls[final_choice].id;

    // ----------- girl_id を上書き（affection 等は reset_build_json で済ませる）-----------
    json_write_string("build.json", "girl_id", gid);
    SDL_Log("[SELECT] build.json finalized (girl_id=%s)", gid);

    if (voice_girl[final_choice])
        Mix_PlayChannel(-1, voice_girl[final_choice], 0);

    finalize_start_ms = SDL_GetTicks();
}

// ==============================================================
// enter
// ==============================================================
void scene_select_enter(void)
{
    // ★ 毎回ここで build.json を完全初期化
    reset_build_json();

    // 以下は既存処理
    start_ms = SDL_GetTicks();
    deadline_ms = start_ms + 15000;

    font_main = ui_load_font("assets/font/main.otf", 36);
    font_timer = ui_load_font("assets/font/main.otf", 48);

    for (int i = 0; i < 3; i++)
        tex_portrait[i] = load_texture(g_renderer, girls[i].portrait_path);

    tex_selected_icon = load_texture(g_renderer, "assets/ui/selected.png");
    tex_spark = load_texture(g_renderer, "assets/effects/spark.png");

    for (int i = 0; i < 3; i++)
        voice_girl[i] = Mix_LoadWAV(girls[i].voice_path);

    focus = 0;
    decided_index = -1;
    finalizing = false;
    pending_finalize = false;
    glow_t = 0.0f;
}

// ==============================================================
// update
// ==============================================================
void scene_select_update(float dt)
{
    Uint32 now = SDL_GetTicks();

    if (!finalizing && !pending_finalize && now >= deadline_ms)
        pending_finalize = true;

    if (pending_finalize)
    {
        finalize_selection();
        pending_finalize = false;
        return;
    }

    if (finalizing)
    {
        if (now - finalize_start_ms >= 5000)
            change_scene(SCENE_CHAT);
        return;
    }

    if (input_is_pressed(SDL_SCANCODE_LEFT))
        focus = (focus + 2) % 3;

    if (input_is_pressed(SDL_SCANCODE_RIGHT))
        focus = (focus + 1) % 3;

    if (input_is_down(SDL_SCANCODE_RETURN) ||
        input_is_down(SDL_SCANCODE_KP_ENTER))
    {
        decided_index = focus;
    }

    glow_t += dt * 0.6f;
    if (glow_t >= 1.0f)
        glow_t -= 1.0f;
}

// ==============================================================
// render
// ==============================================================
void scene_select_render(SDL_Renderer *R)
{
    SDL_SetRenderDrawColor(R, 10, 10, 16, 255);
    SDL_RenderClear(R);

    ui_text_draw(R, font_main, "SELECT", 40, 20);

    int remain = (deadline_ms > SDL_GetTicks())
                     ? (deadline_ms - SDL_GetTicks()) / 1000
                     : 0;

    char buf[32];
    snprintf(buf, sizeof(buf), "00:%02d", remain);
    ui_text_draw(R, font_timer, buf, 1100, 20);

    for (int i = 0; i < 3; i++)
    {
        int base_x = 80 + i * 400;
        int base_y = 120;

        float scale = (i == focus ? 1.07f : 1.0f);
        int w = (int)(320 * scale);
        int h = (int)(480 * scale);

        SDL_Rect dst = {
            base_x + 160 - w / 2,
            base_y + 240 - h / 2,
            w, h};

        if (tex_portrait[i])
            SDL_RenderCopy(R, tex_portrait[i], NULL, &dst);
        else
            SDL_RenderDrawRect(R, &dst);

        ui_text_draw(R, font_main, girls[i].name, base_x, base_y + 500);
        ui_text_draw(R, font_main, girls[i].type, base_x, base_y + 540);

        if (i == focus)
            draw_neon_frame(R, dst, glow_t);

        if (decided_index == i && tex_selected_icon)
        {
            SDL_Rect icon_dst = {
                dst.x + dst.w / 2 - 60,
                dst.y - 60,
                120, 120};
            SDL_RenderCopy(R, tex_selected_icon, NULL, &icon_dst);
        }
    }
}

// ==============================================================
// exit
// ==============================================================
void scene_select_exit(void)
{
    for (int i = 0; i < 3; i++)
        if (tex_portrait[i])
            SDL_DestroyTexture(tex_portrait[i]);

    if (tex_selected_icon)
        SDL_DestroyTexture(tex_selected_icon);
    if (tex_spark)
        SDL_DestroyTexture(tex_spark);

    if (font_main)
        TTF_CloseFont(font_main);
    if (font_timer)
        TTF_CloseFont(font_timer);

    for (int i = 0; i < 3; i++)
        if (voice_girl[i])
            Mix_FreeChunk(voice_girl[i]);
}
