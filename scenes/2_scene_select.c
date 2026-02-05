// scenes/2_scene_select.c
#include "2_scene_select.h"
#include "../core/scene_manager.h"
#include "../core/input.h"
#include "../core/engine.h"
#include "../util/texture.h"
#include "../ui/ui_text.h"
#include "../util/json.h"
#include "../net/net_client.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>

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
// ステータス（基礎）
//   ※画像の「基礎ステータス(確定)」に合わせる
//   - himari: HP120 ATK20 SP14 ST80 (毎ターン+5)
//   - kiritan: HP100 ATK5  SP8  ST40 (毎ターン+20)
//   - sayo: 全部 0（暫定）
// ==============================================================
typedef struct
{
    int hp;
    int atk;
    int sp;
    int st;
    int st_regen; // 毎ターン +X（不要なら削除OK）
} BaseStats;

static BaseStats base_stats_by_index(int idx)
{
    switch (idx)
    {
    case 0: // himari
        return (BaseStats){120, 20, 14, 80, 5};
    case 1: // kiritan
        return (BaseStats){100, 5, 8, 40, 20};
    case 2: // sayo (暫定：全部0)
        return (BaseStats){0, 0, 0, 0, 0};
    default:
        return (BaseStats){0, 0, 0, 0, 0};
    }
}

// ==============================================================
// 移動距離（基礎）
//   - himari: 6マス
//   - kiritan: 3マス
//   - sayo:   暫定（必要なら変更）
// ==============================================================
static int move_range_by_index(int idx)
{
    switch (idx)
    {
    case 0: return 6; // himari
    case 1: return 3; // kiritan
    case 2: return 4; // sayo（暫定。0でも可）
    default: return 3;
    }
}

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

// オンライン対戦: 接続・マッチング状態
static bool connecting_to_server = false;  // サーバ接続中
static bool online_matching = false;       // マッチング待ち
static Uint32 last_connect_attempt_ms = 0; // 最後の接続試行時刻
static int connect_retry_count = 0;        // 接続リトライ回数
#define CONNECT_RETRY_INTERVAL_MS 2000     // 2秒ごとにリトライ

static Uint32 start_ms;
static Uint32 deadline_ms;
static Uint32 finalize_start_ms;


static Mix_Chunk *voice_girl[3] = {NULL};

static float glow_t = 0.0f;

// ==============================================================
// build.json 初期化（状態枠を必ず作る）
//   - statsはネストせず、トップレベルに base/add を置く（json util の互換性優先）
//   - 移動距離(move_range_base)もここで枠を作り、SELECT確定で上書きする
// ==============================================================
static void reset_build_json(void)
{
    FILE *fp = fopen("build.json", "w");
    if (!fp)
    {
        SDL_Log("[SELECT] Failed to create build.json");
        return;
    }

    fputs("{\n", fp);
    fputs("  \"girl_id\": \"\",\n", fp);

    // 初期好感度（あなたの現行仕様：30）
    fputs("  \"affection\": 30,\n", fp);

    // 会話進行系（必要なら残す）
    fputs("  \"phase\": 0,\n", fp);
    fputs("  \"turn_in_phase\": 0,\n", fp);
    fputs("  \"core_used\": false,\n", fp);

    // --- 基礎ステ（SELECT確定で上書き） ---
    fputs("  \"hp_base\": 0,\n", fp);
    fputs("  \"atk_base\": 0,\n", fp);
    fputs("  \"sp_base\": 0,\n", fp);
    fputs("  \"st_base\": 0,\n", fp);
    fputs("  \"st_regen_base\": 0,\n", fp);

    // --- 移動距離（SELECT確定で上書き） ---
    fputs("  \"move_range_base\": 0,\n", fp);

    // --- 配分分（ALLOCATEで増やす） ---
    fputs("  \"hp_add\": 0,\n", fp);
    fputs("  \"atk_add\": 0,\n", fp);
    fputs("  \"sp_add\": 0,\n", fp);
    fputs("  \"st_add\": 0,\n", fp);

    // skills（末尾なのでカンマ無し）
    fputs("  \"skills\": []\n", fp);
    fputs("}\n", fp);

    fclose(fp);
    SDL_Log("[SELECT] build.json initialized");
}

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
// 枠の周回位置
// ==============================================================
static void get_spark_pos(SDL_Rect dst, float t, float *ox, float *oy)
{
    float x = (float)dst.x, y = (float)dst.y, w = (float)dst.w, h = (float)dst.h;
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
// ネオン枠
// ==============================================================
static void draw_neon_frame(SDL_Renderer *R, SDL_Rect dst, float t)
{
    SDL_Color pink = {255, 80, 180, 255};
    SDL_Color blue = {80, 160, 255, 255};

    int x = dst.x, y = dst.y, w = dst.w, h = dst.h;

    for (int i = 0; i < w; i++)
    {
        float p = (float)i / (float)(w - 1);
        SDL_Color col = lerp_color(pink, blue, p);
        SDL_SetRenderDrawColor(R, col.r, col.g, col.b, 255);
        SDL_RenderDrawPoint(R, x + i, y);
        SDL_RenderDrawPoint(R, x + i, y + h);
    }

    SDL_SetRenderDrawColor(R, pink.r, pink.g, pink.b, 255);
    SDL_RenderDrawLine(R, x, y, x, y + h);

    SDL_SetRenderDrawColor(R, blue.r, blue.g, blue.b, 255);
    SDL_RenderDrawLine(R, x + w, y, x + w, y + h);

    if (tex_spark)
    {
        const int trail_count = 12;
        const float trail_gap = 0.022f;
        const float size_base = 42.0f;

        for (int i = 0; i < trail_count; i++)
        {
            float tt = t - trail_gap * i;
            if (tt < 0) tt += 1.0f;

            float cx, cy;
            get_spark_pos(dst, tt, &cx, &cy);

            float intensity = 1.0f - ((float)i / trail_count);
            SDL_Color col = lerp_color(pink, blue, tt);

            SDL_SetTextureColorMod(
                tex_spark,
                (Uint8)(col.r * intensity),
                (Uint8)(col.g * intensity),
                (Uint8)(col.b * intensity));

            SDL_SetTextureAlphaMod(
                tex_spark,
                (Uint8)(255 * powf(intensity, 1.8f)));

            float size = size_base * (0.55f + 0.45f * intensity);
            SDL_Rect sp = {
                (int)(cx - size / 2),
                (int)(cy - size / 2),
                (int)size,
                (int)size};

            SDL_RenderCopy(R, tex_spark, NULL, &sp);
        }
        SDL_SetTextureAlphaMod(tex_spark, 255);
    }
}

// ==============================================================
// 選択確定
//   - girl_id を書く
//   - キャラ別の基礎ステを build.json に注入
//   - 移動距離(move_range_base)も注入
// ==============================================================
static void finalize_selection(void)
{
    if (finalizing) return;
    finalizing = true;

    int idx = (decided_index != -1) ? decided_index : focus;
    if (idx < 0 || idx >= 3) idx = 0;

    decided_index = idx;

    // キャラID
    json_write_string("build.json", "girl_id", girls[idx].id);

    // キャラ別 基礎ステ注入
    BaseStats bs = base_stats_by_index(idx);
    json_write_int("build.json", "hp_base", bs.hp);
    json_write_int("build.json", "atk_base", bs.atk);
    json_write_int("build.json", "sp_base", bs.sp);
    json_write_int("build.json", "st_base", bs.st);
    json_write_int("build.json", "st_regen_base", bs.st_regen);

    // キャラ別 移動距離注入
    int mv = move_range_by_index(idx);
    json_write_int("build.json", "move_range_base", mv);

    SDL_Log("[SELECT] girl=%s base=(HP:%d ATK:%d SP:%d ST:%d regen:%d) move=%d",
            girls[idx].id, bs.hp, bs.atk, bs.sp, bs.st, bs.st_regen, mv);

    if (voice_girl[idx])
        Mix_PlayChannel(-1, voice_girl[idx], 0);

    finalize_start_ms = SDL_GetTicks();
}

// ==============================================================
// enter
// ==============================================================
void scene_select_enter(void)
{
    reset_build_json();

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

    // サーバ接続を試みる（リトライ処理付き）
    connecting_to_server = true;
    online_matching = false;
    connect_retry_count = 0;
    last_connect_attempt_ms = SDL_GetTicks();

    SDL_Log("[SELECT] サーバ接続を開始します: %s:%d", g_net_host, g_net_port);
    net_connect(g_net_host, g_net_port);

    if (net_is_online()) {
        net_send_ready();
        SDL_Log("[SELECT] サーバに接続しました。マッチング待ち...");
        connecting_to_server = false;
        online_matching = true;
    } else {
        SDL_Log("[SELECT] サーバ接続中... (試行 1回目)");
        connect_retry_count = 1;
    }
}

// ==============================================================
// update
// ==============================================================
void scene_select_update(float dt)
{
    // サーバ接続中（リトライ処理）
    if (connecting_to_server) {
        Uint32 now = SDL_GetTicks();

        // 接続成功チェック
        if (net_is_online()) {
            net_send_ready();
            SDL_Log("[SELECT] サーバに接続しました。マッチング待ち...");
            connecting_to_server = false;
            online_matching = true;
            return;
        }

        // リトライ間隔経過後、再接続を試みる
        if (now - last_connect_attempt_ms >= CONNECT_RETRY_INTERVAL_MS) {
            connect_retry_count++;
            last_connect_attempt_ms = now;

            SDL_Log("[SELECT] サーバ接続を再試行します... (試行 %d回目)", connect_retry_count);
            net_connect(g_net_host, g_net_port);

            if (net_is_online()) {
                net_send_ready();
                SDL_Log("[SELECT] サーバに接続しました。マッチング待ち...");
                connecting_to_server = false;
                online_matching = true;
                return;
            }
        }

        return;
    }

    // オンラインマッチング待ち
    if (online_matching) {
        net_poll();
        if (!net_is_online()) {
            SDL_Log("[SELECT] 接続が切断されました。HOMEに戻ります。");
            online_matching = false;
            change_scene(SCENE_HOME);
            return;
        }
        if (net_get_player_id() >= 0) {
            // マッチング成立 → 通常のキャラ選択へ
            SDL_Log("[SELECT] マッチング成立! Player ID: %d", net_get_player_id());
            online_matching = false;
            start_ms = SDL_GetTicks();
            deadline_ms = start_ms + 15000;
        }
        if (input_is_pressed(SDL_SCANCODE_ESCAPE)) {
            SDL_Log("[SELECT] マッチングをキャンセルしました。");
            net_disconnect();
            online_matching = false;
            change_scene(SCENE_HOME);
            return;
        }
        return;
    }

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
    if (glow_t >= 1.0f) glow_t -= 1.0f;
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
                 ? (int)((deadline_ms - SDL_GetTicks()) / 1000)
                 : 0;

    // サーバ接続中表示
    if (connecting_to_server) {
        ui_text_draw(R, font_main, "サーバに接続中...", 440, 340);
        return;
    }

    // マッチング待ち表示
    if (online_matching) {
        ui_text_draw(R, font_main, "マッチング待ち...", 460, 340);
        return;
    }

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
        if (tex_portrait[i]) SDL_DestroyTexture(tex_portrait[i]);

    if (tex_selected_icon) SDL_DestroyTexture(tex_selected_icon);
    if (tex_spark) SDL_DestroyTexture(tex_spark);

    if (font_main) TTF_CloseFont(font_main);
    if (font_timer) TTF_CloseFont(font_timer);

    for (int i = 0; i < 3; i++)
        if (voice_girl[i]) Mix_FreeChunk(voice_girl[i]);
}
