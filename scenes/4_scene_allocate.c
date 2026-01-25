// scenes/4_scene_allocate.c
// =============================================================
// ステータス配分シーン（ALLOCATE）
// - build.json を読み込み、基礎ステ＆好感度(=配分ポイント)を復元
// - ↑↓で項目選択、→で+1、←で-1、Enterで決定（時間切れも確定）
// - 配分は「ステータス実数」を直接増やす（HP+1など）
// - ブロック表示：白(基礎)の右隣から緑(増分)
// - ブロック単位（表示のみ）
//   HP:10, ATK:2, SPD:2, ST:10
// - コスト（+1あたり）
//   HP:1 / ATK:10 / SPD:5 / ST:3
// - 右側に「(+1 cost X)」を表示（残す）
// - 決定の上に「タッグ技を習得する」トグルを追加
//   * 好感度(開始値)が50以上のときだけ選択可能
//   * 習得コストは20（ポイントから差し引き）
// - 保存：build.json に *_add と tag_learned を保存
// =============================================================

#include "4_scene_allocate.h"
#include "../core/engine.h"
#include "../core/input.h"
#include "../core/scene_manager.h"
#include "../util/texture.h"
#include "../util/json.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

// =============================================================
// 調整用
// =============================================================
#define ALLOCATE_TIME_LIMIT_SEC 30.0f
#define REPEAT_DELAY_SEC 0.12f

// ブロック単位（表示のみ）
#define BLOCK_UNIT_HP   10
#define BLOCK_UNIT_ATK  2
#define BLOCK_UNIT_SPD  2
#define BLOCK_UNIT_ST   10

// 横に並べる最大ブロック数（見た目上の上限）
#define BLOCK_MAX 20

// タッグ技
#define TAG_UNLOCK_AFFECTION 50
#define TAG_COST 20

// build.json keys（トップレベル）
#define KEY_AFFECTION   "affection"
#define KEY_HP_BASE     "hp_base"
#define KEY_ATK_BASE    "atk_base"
#define KEY_SP_BASE     "sp_base"     // SPD相当
#define KEY_ST_BASE     "st_base"
#define KEY_HP_ADD      "hp_add"
#define KEY_ATK_ADD     "atk_add"
#define KEY_SP_ADD      "sp_add"
#define KEY_ST_ADD      "st_add"
#define KEY_TAG_LEARNED "tag_learned"

// =============================================================
// 内部
// =============================================================
typedef enum {
    STAT_HP = 0,
    STAT_ATK,
    STAT_SPD,
    STAT_ST,
    STAT_COUNT
} StatId;

typedef enum {
    ITEM_STAT_HP = 0,
    ITEM_STAT_ATK,
    ITEM_STAT_SPD,
    ITEM_STAT_ST,
    ITEM_TAG,     // タッグ技習得
    ITEM_DECIDE,  // 決定
    ITEM_COUNT
} MenuItem;

static const char* g_stat_names[STAT_COUNT] = {"HP","ATK","SPD","ST"};

static int g_base[STAT_COUNT];   // 基礎（build.json由来）
static int g_alloc[STAT_COUNT];  // 増分（実数：+1単位）

static int g_affection_start = 0; // 開始時の好感度（=配分ポイント元）
static int g_points_total = 0;
static int g_points_left  = 0;

static int  g_cursor = 0;   // 0..ITEM_COUNT-1
static bool g_locked = false;

static bool g_tag_unlocked = false;
static bool g_tag_learned  = false;

static float g_time_left = ALLOCATE_TIME_LIMIT_SEC;
static float g_repeat_timer = 0.0f;

static TTF_Font* g_font = NULL;
static SDL_Texture* g_bg = NULL;

// =============================================================
// ステ別パラメータ
// =============================================================
static int stat_cost_per_1(StatId s)
{
    switch (s)
    {
    case STAT_HP:  return 1;
    case STAT_ATK: return 10;
    case STAT_SPD: return 5;
    case STAT_ST:  return 3;   // ST は +1 = 3
    default:       return 1;
    }
}

static int stat_block_unit(StatId s)
{
    switch (s)
    {
    case STAT_HP:  return BLOCK_UNIT_HP;
    case STAT_ATK: return BLOCK_UNIT_ATK;
    case STAT_SPD: return BLOCK_UNIT_SPD;
    case STAT_ST:  return BLOCK_UNIT_ST;
    default:       return 1;
    }
}

// すべて +1 ずつ
static int stat_step(StatId s)
{
    (void)s;
    return 1;
}

// =============================================================
// build.json 読み込み
// =============================================================
static int safe_read_int_or(const char* key, int fallback)
{
    int v = fallback;
    if (json_read_int("build.json", key, &v)) return v;
    return fallback;
}

static void load_from_build_json(void)
{
    // 基礎：読めなければ0
    g_base[STAT_HP]  = safe_read_int_or(KEY_HP_BASE, 0);
    g_base[STAT_ATK] = safe_read_int_or(KEY_ATK_BASE, 0);
    g_base[STAT_SPD] = safe_read_int_or(KEY_SP_BASE, 0);
    g_base[STAT_ST]  = safe_read_int_or(KEY_ST_BASE, 0);

    // 増分：読めなければ0
    g_alloc[STAT_HP]  = safe_read_int_or(KEY_HP_ADD, 0);
    g_alloc[STAT_ATK] = safe_read_int_or(KEY_ATK_ADD, 0);
    g_alloc[STAT_SPD] = safe_read_int_or(KEY_SP_ADD, 0);
    g_alloc[STAT_ST]  = safe_read_int_or(KEY_ST_ADD, 0);

    // 好感度：読めなければ30
    g_affection_start = safe_read_int_or(KEY_AFFECTION, 30);
    if (g_affection_start < 0) g_affection_start = 0;

    // タッグ：読めなければOFF
    g_tag_learned = (safe_read_int_or(KEY_TAG_LEARNED, 0) != 0);

    // 解放条件
    g_tag_unlocked = (g_affection_start >= TAG_UNLOCK_AFFECTION);
    if (!g_tag_unlocked) g_tag_learned = false; // ロック中は強制OFF
}

static int calc_used_cost_points(void)
{
    // ステ増分の「コスト合計」を計算（add は実数+1単位）
    int used = 0;
    for (int i = 0; i < STAT_COUNT; i++)
    {
        int c = stat_cost_per_1((StatId)i);
        int a = g_alloc[i];
        if (a < 0) a = 0;
        used += a * c;
    }
    if (g_tag_learned) used += TAG_COST;
    return used;
}

static void recompute_points_left(void)
{
    g_points_total = g_affection_start;
    int used = calc_used_cost_points();
    g_points_left = g_points_total - used;
    if (g_points_left < 0) g_points_left = 0;
}

// =============================================================
// build.json 保存
// =============================================================
static void save_to_build_json(void)
{
    json_write_int("build.json", KEY_HP_ADD,  g_alloc[STAT_HP]);
    json_write_int("build.json", KEY_ATK_ADD, g_alloc[STAT_ATK]);
    json_write_int("build.json", KEY_SP_ADD,  g_alloc[STAT_SPD]);
    json_write_int("build.json", KEY_ST_ADD,  g_alloc[STAT_ST]);

    json_write_int("build.json", KEY_TAG_LEARNED, g_tag_learned ? 1 : 0);
}

// =============================================================
// カーソル/選択制御
// =============================================================
static bool item_is_selectable(int item)
{
    if (item == ITEM_TAG) {
        return g_tag_unlocked; // 好感度50以上のときだけ選択可
    }
    return true;
}

// カーソル移動：ロック中項目（タッグ）をスキップ
static void move_cursor(int dir) // dir: -1 or +1
{
    int start = g_cursor;
    for (int k = 0; k < ITEM_COUNT; k++)
    {
        g_cursor += dir;
        if (g_cursor < 0) g_cursor = ITEM_COUNT - 1;
        if (g_cursor >= ITEM_COUNT) g_cursor = 0;

        if (item_is_selectable(g_cursor)) return;
    }
    g_cursor = start;
}

// =============================================================
// テキスト描画
// =============================================================
static void draw_text(SDL_Renderer* r, int x, int y, const char* s, SDL_Color col)
{
    if (!g_font || !s || !s[0]) return;

    SDL_Surface* surf = TTF_RenderUTF8_Blended(g_font, s, col);
    if (!surf) return;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    if (!tex) { SDL_FreeSurface(surf); return; }

    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_RenderCopy(r, tex, NULL, &dst);

    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

static SDL_Color col_white(void){ return (SDL_Color){255,255,255,255}; }
static SDL_Color col_hi(void){ return (SDL_Color){160,255,160,255}; }
static SDL_Color col_dim(void){ return (SDL_Color){160,160,160,255}; }

// =============================================================
// ブロック描画（白の右隣から緑）
// =============================================================
static void draw_blocks_side(SDL_Renderer* r, int x, int y, int unit, int base, int alloc)
{
    int base_blocks  = (unit > 0) ? (base  / unit) : 0;
    int alloc_blocks = (unit > 0) ? (alloc / unit) : 0;

    if (base_blocks < 0) base_blocks = 0;
    if (alloc_blocks < 0) alloc_blocks = 0;

    if (base_blocks > BLOCK_MAX) base_blocks = BLOCK_MAX;
    if (base_blocks + alloc_blocks > BLOCK_MAX) alloc_blocks = BLOCK_MAX - base_blocks;
    if (alloc_blocks < 0) alloc_blocks = 0;

    SDL_Rect frame = {x-6, y-6, (BLOCK_MAX*18)+12, 24};
    SDL_SetRenderDrawColor(r, 50, 50, 50, 255);
    SDL_RenderFillRect(r, &frame);

    // 下地
    for (int i=0;i<BLOCK_MAX;i++) {
        SDL_Rect b = {x + i*18, y, 16, 12};
        SDL_SetRenderDrawColor(r, 80, 80, 80, 255);
        SDL_RenderFillRect(r, &b);
    }

    // 白
    for (int i=0;i<base_blocks;i++) {
        SDL_Rect b = {x + i*18, y, 16, 12};
        SDL_SetRenderDrawColor(r, 230, 230, 230, 255);
        SDL_RenderFillRect(r, &b);
    }

    // 緑（白の右隣）
    for (int i=0;i<alloc_blocks;i++) {
        int idx = base_blocks + i;
        if (idx < 0 || idx >= BLOCK_MAX) break;
        SDL_Rect b = {x + idx*18, y, 16, 12};
        SDL_SetRenderDrawColor(r, 80, 220, 120, 255);
        SDL_RenderFillRect(r, &b);
    }
}

// =============================================================
// 増減（+1ずつ / コストはステ別）
// =============================================================
static void inc_stat(StatId s)
{
    if (s < 0 || s >= STAT_COUNT) return;

    int step = stat_step(s);              // 1
    int cost = step * stat_cost_per_1(s);

    if (g_points_left < cost) return;

    g_alloc[s] += step;
    g_points_left -= cost;
}

static void dec_stat(StatId s)
{
    if (s < 0 || s >= STAT_COUNT) return;

    int step = stat_step(s); // 1
    if (g_alloc[s] < step) return;

    int cost = step * stat_cost_per_1(s);

    g_alloc[s] -= step;
    g_points_left += cost;
}

// =============================================================
// タッグ技トグル
// =============================================================
static void toggle_tag_skill(void)
{
    if (!g_tag_unlocked) return;

    if (!g_tag_learned) {
        if (g_points_left < TAG_COST) return;
        g_tag_learned = true;
        g_points_left -= TAG_COST;
    } else {
        g_tag_learned = false;
        g_points_left += TAG_COST;
    }
}

// =============================================================
// 保存＆遷移
// =============================================================
static void finalize_and_go_next(void)
{
    save_to_build_json();

    g_locked = true;

    // 次シーン（バトルへ）
    change_scene(SCENE_BATTLE);
}

// =============================================================
// scene API
// =============================================================
void scene_allocate_enter(void)
{
    printf("[ALLOCATE] enter\n");

    // build.json から復元
    load_from_build_json();

    // ポイント再計算（既に add / tag が入っていた場合も整合）
    recompute_points_left();

    g_locked = false;
    g_time_left = ALLOCATE_TIME_LIMIT_SEC;
    g_repeat_timer = 0.0f;

    // 初期カーソル：タッグがロックなら飛ばす
    g_cursor = 0;
    if (!item_is_selectable(g_cursor)) move_cursor(+1);

    g_font = TTF_OpenFont("assets/font/main.ttf", 28);
    if (!g_font) {
        printf("[ALLOCATE] TTF_OpenFont failed: %s\n", TTF_GetError());
    }

    g_bg = load_texture(g_renderer, "assets/ui/allocate_bg.png");
}

// ※あなたの既存コード互換のため残す（scene_manager が exit を呼ぶなら exit を使う）
void scene_allocate_leave(void)
{
    if (g_font) { TTF_CloseFont(g_font); g_font=NULL; }
    if (g_bg)   { SDL_DestroyTexture(g_bg); g_bg=NULL; }
}
void scene_allocate_exit(void)
{
    scene_allocate_leave();
}

void scene_allocate_update(float dt)
{
    // タイムアウトで強制確定
    if (!g_locked) {
        g_time_left -= dt;
        if (g_time_left <= 0.0f) {
            g_time_left = 0.0f;
            finalize_and_go_next();
            return;
        }
    }
    if (g_locked) return;

    g_repeat_timer -= dt;

    // 上下でカーソル
    if (input_is_pressed(SDL_SCANCODE_UP))   move_cursor(-1);
    if (input_is_pressed(SDL_SCANCODE_DOWN)) move_cursor(+1);

    // 左右で増減（押しっぱなし + リピート制御）
    if (g_repeat_timer <= 0.0f) {

        // ステ項目
        if (g_cursor >= ITEM_STAT_HP && g_cursor <= ITEM_STAT_ST) {
            int si = g_cursor; // 0..3

            if (input_is_pressed(SDL_SCANCODE_RIGHT) || input_is_down(SDL_SCANCODE_RIGHT)) {
                inc_stat((StatId)si);
                g_repeat_timer = REPEAT_DELAY_SEC;
            }
            if (input_is_pressed(SDL_SCANCODE_LEFT) || input_is_down(SDL_SCANCODE_LEFT)) {
                dec_stat((StatId)si);
                g_repeat_timer = REPEAT_DELAY_SEC;
            }
        }

        // タッグ項目：左右 or Enterでトグル（ロック中は何もしない）
        if (g_cursor == ITEM_TAG) {
            if (input_is_pressed(SDL_SCANCODE_RIGHT) || input_is_pressed(SDL_SCANCODE_LEFT) || input_is_pressed(SDL_SCANCODE_RETURN)) {
                toggle_tag_skill();
                g_repeat_timer = REPEAT_DELAY_SEC;
            }
        }
    }

    // 決定
    if (input_is_pressed(SDL_SCANCODE_RETURN) && g_cursor == ITEM_DECIDE) {
        finalize_and_go_next();
        return;
    }
}

void scene_allocate_render(SDL_Renderer *r)
{
    // 背景
    if (g_bg) {
        SDL_Rect dst = {0,0,1280,720};
        SDL_RenderCopy(r, g_bg, NULL, &dst);
    } else {
        SDL_SetRenderDrawColor(r, 70, 70, 70, 255);
        SDL_RenderClear(r);
    }

    char buf[256];

    // ヘッダ
    snprintf(buf, sizeof(buf), "好感度=%d  配分ポイント：%d（残り %d）",
             g_affection_start, g_points_total, g_points_left);
    draw_text(r, 40, 30, buf, col_white());

    int sec = (int)(g_time_left + 0.999f);
    if (sec < 0) sec = 0;
    snprintf(buf, sizeof(buf), "残り %02d 秒", sec);
    draw_text(r, 1050, 30, buf, col_white());

    // レイアウト
    const int base_x = 80;
    const int base_y = 120;
    const int row_h  = 90;

    // =========================
    // ステータス4行
    // =========================
    for (int i=0;i<STAT_COUNT;i++)
    {
        bool sel = (g_cursor == i);

        if (sel) {
            SDL_Rect hl = {40, base_y + i*row_h - 10, 1200, 70};
            SDL_SetRenderDrawColor(r, 90, 90, 90, 255);
            SDL_RenderFillRect(r, &hl);
        }

        StatId sid = (StatId)i;

        // 左：ラベル
        snprintf(buf, sizeof(buf), "%s", g_stat_names[i]);
        draw_text(r, base_x, base_y + i*row_h, buf, sel ? col_hi() : col_white());

        // 中：ブロック
        draw_blocks_side(r,
                         base_x + 120,
                         base_y + i*row_h + 18,
                         stat_block_unit(sid),
                         g_base[i],
                         g_alloc[i]);

        // 右：最終値
        snprintf(buf, sizeof(buf), "%d", g_base[i] + g_alloc[i]);
        draw_text(r, 980, base_y + i*row_h, buf, col_hi());

        // 右：コスト表示（残す）
        {
            int step = stat_step(sid); // 1
            int cost = step * stat_cost_per_1(sid);
            snprintf(buf, sizeof(buf), "(+%d cost %d)", step, cost);
            draw_text(r, 1080, base_y + i*row_h, buf, col_white());
        }
    }

    // =========================
    // タッグ技（決定の上）
    // =========================
    {
        int y = base_y + STAT_COUNT*row_h;

        bool sel = (g_cursor == ITEM_TAG);
        SDL_Color c = col_white();

        if (!g_tag_unlocked) c = col_dim();
        if (sel) c = col_hi();

        // ハイライト
        if (sel) {
            SDL_Rect hl = {40, y - 10, 1200, 70};
            SDL_SetRenderDrawColor(r, 90, 90, 90, 255);
            SDL_RenderFillRect(r, &hl);
        }

        if (!g_tag_unlocked) {
            snprintf(buf, sizeof(buf), "タッグ技を習得する（好感度%d以上で解放）", TAG_UNLOCK_AFFECTION);
        } else {
            snprintf(buf, sizeof(buf), "タッグ技を習得する：%s", g_tag_learned ? "ON" : "OFF");
        }
        draw_text(r, base_x, y, buf, c);

        // コスト表示（右）
        snprintf(buf, sizeof(buf), "(cost %d)", TAG_COST);
        draw_text(r, 1080, y, buf, (!g_tag_unlocked) ? col_dim() : col_white());
    }

    // =========================
    // 決定ボタン
    // =========================
    {
        int y = base_y + (STAT_COUNT+1)*row_h;

        bool sel = (g_cursor == ITEM_DECIDE);
        SDL_Rect btn = {420, y - 10, 440, 60};

        SDL_SetRenderDrawColor(r, sel ? 120 : 90, sel ? 120 : 90, sel ? 120 : 90, 255);
        SDL_RenderFillRect(r, &btn);

        SDL_SetRenderDrawColor(r, 30, 30, 30, 255);
        SDL_RenderDrawRect(r, &btn);

        draw_text(r, btn.x + 160, btn.y + 15, "決定", sel ? col_hi() : col_white());
    }

    // 操作説明
    draw_text(r, 40, 690, "操作：↑↓選択  →+1  ←-1  Enter=決定（タッグは左右/EnterでON/OFF）", col_white());
}
