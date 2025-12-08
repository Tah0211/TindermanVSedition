#include "2_scene_select.h"
#include "../core/scene_manager.h"
#include "../core/input.h"
#include "../core/engine.h"
#include "../util/texture.h"
#include "../util/json.h"
#include "../net/http_client.h"
#include "../ui/ui_text.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <stdbool.h>

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

static int focus = 0;          // カーソル位置
static int decided_index = -1; // Enter予約のキャラ

static bool finalizing = false;       // 最終決定後
static bool pending_finalize = false; // finalize予約フラグ

static Uint32 start_ms;
static Uint32 deadline_ms;
static Uint32 finalize_start_ms;

static Mix_Chunk *voice_girl[3] = {NULL};

static float glow_t = 0.0f;

// ==============================================================
// 色補間
// ==============================================================
static SDL_Color lerp_color(SDL_Color a, SDL_Color b, float t)
{
    SDL_Color c;
    c.r = (Uint8)(a.r + (b.r - a.r) * t);
    c.g = (Uint8)(a.g + (b.g - a.g) * t);
    c.b = (Uint8)(a.b + (b.b - a.b) * t);
    c.a = 255;
    return c;
}

// ==============================================================
// 枠周りスパーク位置
// ==============================================================
static void get_spark_pos(SDL_Rect dst, float t, float *ox, float *oy)
{
    float x = dst.x, y = dst.y, w = dst.w, h = dst.h;
    float cx, cy;

    t = fmodf(t, 1.0f);

    if (t < 0.25f)
    {
        float p = t / 0.25f;
        cx = x + w * p;
        cy = y;
    }
    else if (t < 0.50f)
    {
        float p = (t - 0.25f) / 0.25f;
        cx = x + w;
        cy = y + h * p;
    }
    else if (t < 0.75f)
    {
        float p = (t - 0.50f) / 0.25f;
        cx = x + w * (1 - p);
        cy = y + h;
    }
    else
    {
        float p = (t - 0.75f) / 0.25f;
        cx = x;
        cy = y + h * (1 - p);
    }

    *ox = cx;
    *oy = cy;
}

// ==============================================================
// ネオン枠
// ==============================================================
static void draw_neon_frame(SDL_Renderer *R, SDL_Rect dst, float t)
{
    SDL_Color pink = {255, 80, 180, 255};
    SDL_Color blue = {80, 160, 255, 255};

    int x = dst.x, y = dst.y, w = dst.w, h = dst.h;

    // 上下の横グラデ
    for (int i = 0; i < w; i++)
    {
        float p = (float)i / (float)(w - 1);
        SDL_Color col = lerp_color(pink, blue, p);

        SDL_SetRenderDrawColor(R, col.r, col.g, col.b, 255);
        SDL_RenderDrawPoint(R, x + i, y);
        SDL_RenderDrawPoint(R, x + i, y + h);
    }

    // 左右単色
    SDL_SetRenderDrawColor(R, pink.r, pink.g, pink.b, 255);
    SDL_RenderDrawLine(R, x, y, x, y + h);

    SDL_SetRenderDrawColor(R, blue.r, blue.g, blue.b, 255);
    SDL_RenderDrawLine(R, x + w, y, x + w, y + h);

    // スパーク
    if (tex_spark)
    {
        float cx, cy;
        get_spark_pos(dst, t, &cx, &cy);

        SDL_Rect sp = {(int)(cx - 20), (int)(cy - 20), 40, 40};
        SDL_RenderCopy(R, tex_spark, NULL, &sp);
    }
}

// ==============================================================
// ★最終決定処理（タイムアップ時のみ呼ぶ）
// ==============================================================
static void finalize_selection(void)
{
    if (finalizing)
        return;

    finalizing = true;

    int final_choice = (decided_index != -1) ? decided_index : focus;
    decided_index = final_choice;

    json_write_string("selected_girl.json", "id", girls[final_choice].id);
    http_post_phase_done("select");

    // ★ デバッグログ
    SDL_Log("INFO: finalize_selection → play voice for %s (%s)",
            girls[final_choice].name,
            girls[final_choice].voice_path);

    if (voice_girl[final_choice])
    {
        int ch = Mix_PlayChannel(-1, voice_girl[final_choice], 0);

        if (ch == -1)
        {
            SDL_Log("ERR: Mix_PlayChannel failed: %s", Mix_GetError());
        }
        else
        {
            SDL_Log("INFO: Mix_PlayChannel on ch=%d", ch);
        }
    }
    else
    {
        SDL_Log("ERR: voice_girl[%d] is NULL", final_choice);
    }

    finalize_start_ms = SDL_GetTicks();
}

// ==============================================================
// enter 関数
// ==============================================================
void scene_select_enter(void)
{
    start_ms = SDL_GetTicks();
    deadline_ms = start_ms + 15000;

    font_main = ui_load_font("assets/font/main.otf", 36);
    font_timer = ui_load_font("assets/font/main.otf", 48);

    for (int i = 0; i < 3; i++)
        tex_portrait[i] = load_texture(g_renderer, girls[i].portrait_path);

    tex_selected_icon = load_texture(g_renderer, "assets/ui/selected.png");
    tex_spark = load_texture(g_renderer, "assets/effects/spark.png");

    for (int i = 0; i < 3; i++)
    {
        voice_girl[i] = Mix_LoadWAV(girls[i].voice_path);

        if (!voice_girl[i])
        {
            SDL_Log("ERR: Failed to load WAV for %s (%s) → %s",
                    girls[i].name,
                    girls[i].voice_path,
                    Mix_GetError());
        }
        else
        {
            SDL_Log("OK: Loaded WAV for %s", girls[i].name);
        }
    }

    focus = 0;
    decided_index = -1;
    finalizing = false;
    pending_finalize = false;
    glow_t = 0.0f;
}

// ==============================================================
// update 関数（タイムアップ・5秒待機の核）
// ==============================================================
void scene_select_update(float dt)
{
    Uint32 now = SDL_GetTicks();

    // -----------------------------
    // ① タイムアップ → finalize予約
    // -----------------------------
    if (!finalizing && !pending_finalize && now >= deadline_ms)
    {
        pending_finalize = true;
    }

    // -----------------------------
    // ② finalize予約 → 次フレームで1回だけ実行
    // -----------------------------
    if (pending_finalize)
    {
        finalize_selection();
        pending_finalize = false;
        return; // finalizeしたフレームでは入力させない
    }

    // -----------------------------
    // ③ finalizing（確定後5秒待機）
    // -----------------------------
    if (finalizing)
    {
        if (now - finalize_start_ms >= 5000)
            change_scene(SCENE_CHAT);

        return; // finalizing中は入力禁止
    }

    // -----------------------------
    // ④ 通常の選択操作
    // -----------------------------
    if (input_is_pressed(SDL_SCANCODE_LEFT))
        focus = (focus + 2) % 3;

    if (input_is_pressed(SDL_SCANCODE_RIGHT))
        focus = (focus + 1) % 3;

    // Enterはあくまで予約
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
// render 関数
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

        SDL_RenderCopy(R, tex_portrait[i], NULL, &dst);

        ui_text_draw(R, font_main, girls[i].name, base_x, base_y + 500);
        ui_text_draw(R, font_main, girls[i].type, base_x, base_y + 540);

        if (i == focus)
            draw_neon_frame(R, dst, glow_t);

        // 最終決定されたキャラがいたら SELECTED 表示
        if (decided_index == i)
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
// exit 関数
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
