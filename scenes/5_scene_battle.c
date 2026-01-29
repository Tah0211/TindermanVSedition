// scenes/5_scene_battle.c
#include "5_scene_battle.h"

#include "../core/input.h"
#include "../core/scene_manager.h"
#include "../ui/ui_text.h"
#include "../util/json.h"

#include "battle/battle_core.h"
#include "battle/battle_skills.h"
#include "battle/cutin.h"
#include "battle/char_defs.h"   // ★追加：char_defs に合わせる

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h> // abs
#include <math.h>   // fabsf, roundf, expf, lroundf

// ===============================
//  表示/フォント
// ===============================
static TTF_Font *g_font = NULL;

// ===============================
//  Battle Core（純ロジック）
// ===============================
static BattleCore g_core;
static bool g_inited = false;

// ===============================
//  Cutin context（演出）
// ===============================
static CutinContext g_cutin;

// ===============================
//  ローカル宣言（いまはローカル用）
// ===============================
static TurnCmd g_p1_cmd;
static TurnCmd g_p2_cmd;
static bool g_p1_locked = false;
static bool g_p2_locked = false;

// ===============================
//  21x21 グリッド
// ===============================
#define GRID_W 21
#define GRID_H 21

// ===============================
//  主人公固定ステ（1P/2P共通）
// ===============================
#define HERO_HP_MAX   150
#define HERO_ATK      10
#define HERO_SPD      10
#define HERO_ST_MAX   100

// 主人公固定移動距離（1P/2P共通）
#define HERO_MOVE_RANGE 4

// 初期配置（仕様固定）
#define INIT_P1_X 0
#define INIT_P2_X 20
#define INIT_HERO_Y 10
#define INIT_GIRL_Y 12

// ===============================
//  UI状態
//  攻撃/待機：コマンド → 移動 →（攻撃なら）技 →（主人公→相棒）→ 最終確認
// ===============================
typedef enum {
    UI_CMD_SELECT = 0,
    UI_MOVE_SELECT,
    UI_SKILL_SELECT,
    UI_TURN_CONFIRM
} BattleUiState;

typedef enum {
    CMD_ATTACK = 0,
    CMD_WAIT
} SimpleCmd;

typedef enum {
    CONFIRM_YES = 0,
    CONFIRM_NO  = 1
} ConfirmChoice;

static BattleUiState  g_ui = UI_CMD_SELECT;
static SimpleCmd      g_cmd = CMD_ATTACK;
static ConfirmChoice  g_confirm = CONFIRM_YES;

// 行動中ユニット（固定順）
static Slot g_act_slot = SLOT_HERO;

// 選択情報（攻撃時の技とターゲット）
static int g_skill_index[2] = {0, 0};  // slotごとのskill index
static int g_target = 0;               // 0=enemy hero, 1=enemy girl（単体用）

// 移動カーソル
static Pos g_move_to = {0, 0};

// ===============================
//  P1 予測（作戦決定：実位置は動かさず半透明ゴースト表示）
// ===============================
typedef struct {
    bool  decided;
    bool  has_move;
    Pos   move_to;
    int8_t skill_index; // -1=技なし（待機）
    int8_t target;      // -1=なし
} UnitPlan;

static UnitPlan g_plan_p1[2];
static bool g_preview_active[2] = {false, false};
static Pos  g_preview_pos[2];

// ===============================
//  Undo（ターン内1手戻し）
// ===============================
#define UNDO_MAX 64

typedef struct {
    BattleUiState ui;
    SimpleCmd cmd;
    Slot act_slot;

    int skill_index[2];
    int target;
    Pos move_to;

    UnitPlan plan_p1[2];
    bool preview_active[2];
    Pos  preview_pos[2];

    bool p1_locked;
    ConfirmChoice confirm;
} BattleUiSnapshot;

static BattleUiSnapshot g_undo[UNDO_MAX];
static int g_undo_top = 0;

static void snapshot_capture(BattleUiSnapshot *s)
{
    s->ui = g_ui;
    s->cmd = g_cmd;
    s->act_slot = g_act_slot;

    s->skill_index[0] = g_skill_index[0];
    s->skill_index[1] = g_skill_index[1];
    s->target = g_target;
    s->move_to = g_move_to;

    memcpy(s->plan_p1, g_plan_p1, sizeof(g_plan_p1));
    s->preview_active[0] = g_preview_active[0];
    s->preview_active[1] = g_preview_active[1];
    s->preview_pos[0] = g_preview_pos[0];
    s->preview_pos[1] = g_preview_pos[1];

    s->p1_locked = g_p1_locked;
    s->confirm = g_confirm;
}

static void snapshot_apply(const BattleUiSnapshot *s)
{
    g_ui = s->ui;
    g_cmd = s->cmd;
    g_act_slot = s->act_slot;

    g_skill_index[0] = s->skill_index[0];
    g_skill_index[1] = s->skill_index[1];
    g_target = s->target;
    g_move_to = s->move_to;

    memcpy(g_plan_p1, s->plan_p1, sizeof(g_plan_p1));
    g_preview_active[0] = s->preview_active[0];
    g_preview_active[1] = s->preview_active[1];
    g_preview_pos[0] = s->preview_pos[0];
    g_preview_pos[1] = s->preview_pos[1];

    g_p1_locked = s->p1_locked;
    g_confirm = s->confirm;
}

static void undo_clear(void)
{
    g_undo_top = 0;
}

static void undo_push(void)
{
    if (g_undo_top >= UNDO_MAX) return;
    snapshot_capture(&g_undo[g_undo_top++]);
}

static bool undo_pop(void)
{
    // 初期状態は残す
    if (g_undo_top <= 1) return false;

    g_undo_top--;
    snapshot_apply(&g_undo[g_undo_top - 1]);
    return true;
}

// ===============================
//  実行フェーズ（SPD順に移動→攻撃/待機）
// ===============================
typedef enum {
    EXE_NONE = 0,
    EXE_MOVE,
    EXE_ACT
} ExecStage;

static bool g_exec_active = false;
static ExecStage g_exec_stage = EXE_NONE;
static int g_exec_order[4];
static int g_exec_n = 0;          // aliveのみの実行数
static int g_exec_i = 0;

// 見た目用：実行中の描画座標（floatで保持）
static SDL_FPoint g_anim_pos_f[4];

// 今ターンの命令（移動先）
static bool g_cmd_has_move[4];
static Pos  g_cmd_move_to[4];

// 「作戦決定」開始時点の位置（ロジックが先に座標を書き換えても演出できるように保持）
static Pos g_pre_step_pos[4];
static SDL_FPoint g_pre_step_pos_f[4];

// 速度（セル/秒）
static float g_move_speed_cells = 8.0f;

// 行動（攻撃/待機）の見せ時間
static float g_act_pause_sec  = 0.35f;
static float g_act_pause_left = 0.0f;

// ===============================
//  HP/ST バーをヌルっと（表示用追従値）
// ===============================
static float g_disp_hp[4];
static float g_disp_st[4];

// 追従速度（大きいほど速く追従）
static float g_bar_lerp_hp = 10.0f;
static float g_bar_lerp_st = 14.0f;

static void bars_sync_to_real(void)
{
    for (int i = 0; i < 4; i++) {
        g_disp_hp[i] = (float)g_core.units[i].stats.hp;
        g_disp_st[i] = (float)g_core.units[i].stats.st;
    }
}

static void bars_update(float dt)
{
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;

    float ah = 1.0f - expf(-g_bar_lerp_hp * dt);
    float as = 1.0f - expf(-g_bar_lerp_st * dt);

    for (int i = 0; i < 4; i++) {
        float thp = (float)g_core.units[i].stats.hp;
        float tst = (float)g_core.units[i].stats.st;

        g_disp_hp[i] += (thp - g_disp_hp[i]) * ah;
        g_disp_st[i] += (tst - g_disp_st[i]) * as;
    }
}

// ===============================
//  JSON
// ===============================
static int read_int_or(const char *key, int fallback)
{
    int v = fallback;
    json_read_int("build.json", key, &v);
    return v;
}

// ★このシーン内で「どの陣営がタッグ習得してるか」を保持（char_defs用）
static bool g_p1_tag_learned = false;
static bool g_p2_tag_learned = false;

static void read_girl_info(char *out_id, size_t out_sz, bool *out_tag)
{
    snprintf(out_id, out_sz, "himari");
    *out_tag = false;

    int tag = 0;
    json_read_int("build.json", "tag_learned", &tag);
    *out_tag = (tag != 0);
}

// 相棒の移動距離
static int get_partner_move_range(void)
{
    int mv = read_int_or("move_range_base", 3);
    if (mv < 0) mv = 0;
    if (mv > 20) mv = 20;
    return mv;
}

static int get_move_range_for_unit(const Unit *u)
{
    if (u->slot == SLOT_HERO) return HERO_MOVE_RANGE;
    return get_partner_move_range();
}

// ★ユニットに対して「タッグ習得済み扱いか」を返す
static bool is_tag_learned_for_unit(const Unit *u)
{
    if (!u) return false;
    if (u->slot == SLOT_HERO) return false; // heroは tag_skill_id=NULL なので常にfalseでOK
    return (u->team == TEAM_P1) ? g_p1_tag_learned : g_p2_tag_learned;
}

// ★char_defs に合わせた「技数」
static int get_skill_count_for_unit(const Unit *u)
{
    if (!u) return 0;
    const CharDef *cd = char_def_get(u->char_id);
    if (!cd) {
        // 定義が無い場合は安全に0扱い（UI上は最低1にクランプする箇所あり）
        return 0;
    }
    return char_def_get_available_skill_count(cd, is_tag_learned_for_unit(u));
}

static int calc_unit_max_hp(const Unit *u)
{
    if (u->slot == SLOT_HERO) return HERO_HP_MAX;

    int max_hp = read_int_or("hp_base", 100) + read_int_or("hp_add", 0);
    if (max_hp < 1) max_hp = 1;
    return max_hp;
}

static int calc_unit_max_st(const Unit *u)
{
    if (u->slot == SLOT_HERO) return HERO_ST_MAX;

    int max_st = read_int_or("st_base", 30) + read_int_or("st_add", 0);
    if (max_st < 1) max_st = 1;
    return max_st;
}

// ===============================
//  位置/距離
// ===============================
static bool is_move_in_range(const Pos from, const Pos to, int mv)
{
    int dx = abs((int)to.x - (int)from.x);
    int dy = abs((int)to.y - (int)from.y);
    return (dx + dy) <= mv;
}

static void clamp_move_cursor(Pos *p)
{
    if (p->x < 0)  p->x = 0;
    if (p->x > 20) p->x = 20;
    if (p->y < 0)  p->y = 0;
    if (p->y > 20) p->y = 20;
}

// ===============================
//  プラン初期化
// ===============================
static void reset_p1_plan(void)
{
    memset(g_plan_p1, 0, sizeof(g_plan_p1));
    g_plan_p1[SLOT_HERO].skill_index = -1;
    g_plan_p1[SLOT_GIRL].skill_index = -1;
    g_plan_p1[SLOT_HERO].target = -1;
    g_plan_p1[SLOT_GIRL].target = -1;

    g_preview_active[SLOT_HERO] = false;
    g_preview_active[SLOT_GIRL] = false;

    g_act_slot = SLOT_HERO;
    g_ui = UI_CMD_SELECT;
    g_cmd = CMD_ATTACK;
    g_confirm = CONFIRM_YES;

    g_p1_locked = false;

    undo_clear();
    undo_push();
}

static Pos get_p1_unit_draw_pos(const Unit *u)
{
    return u->pos;
}

// ===============================
//  Core init
// ===============================
static void init_battle_core(void)
{
    if (!g_font) g_font = ui_load_font("assets/font/main.otf", 28);

    Stats p1h, p1g, p2h, p2g;

    p1h.hp  = HERO_HP_MAX;
    p1h.atk = HERO_ATK;
    p1h.spd = HERO_SPD;
    p1h.st  = HERO_ST_MAX;

    {
        int hp_base  = read_int_or("hp_base",  100);
        int atk_base = read_int_or("atk_base", 10);
        int sp_base  = read_int_or("sp_base",  10);
        int st_base  = read_int_or("st_base",  30);

        int hp_add  = read_int_or("hp_add",  0);
        int atk_add = read_int_or("atk_add", 0);
        int sp_add  = read_int_or("sp_add",  0);
        int st_add  = read_int_or("st_add",  0);

        p1g.hp  = hp_base  + hp_add;
        p1g.atk = atk_base + atk_add;
        p1g.spd = sp_base  + sp_add;
        p1g.st  = st_base  + st_add;
    }

    p2h = p1h;
    p2g = p1g;

    char p1_girl_id[32];
    bool p1_tag = false;
    read_girl_info(p1_girl_id, sizeof(p1_girl_id), &p1_tag);

    const char *p2_girl_id = "kiritan";
    bool p2_tag = false;

    // ★char_defs 用に保持
    g_p1_tag_learned = p1_tag;
    g_p2_tag_learned = p2_tag;

    bool ok = battle_core_init(&g_core,
                              p1_girl_id, p1_tag, p1h, p1g,
                              p2_girl_id, p2_tag, p2h, p2g);
    if (!ok) printf("[BATTLE] battle_core_init FAILED\n");

    g_core.units[0].pos = (Pos){ INIT_P1_X, INIT_HERO_Y };
    g_core.units[1].pos = (Pos){ INIT_P1_X, INIT_GIRL_Y };
    g_core.units[2].pos = (Pos){ INIT_P2_X, INIT_HERO_Y };
    g_core.units[3].pos = (Pos){ INIT_P2_X, INIT_GIRL_Y };

    for (int i = 0; i < 4; i++) {
        g_pre_step_pos[i] = g_core.units[i].pos;

        g_anim_pos_f[i].x = (float)g_core.units[i].pos.x;
        g_anim_pos_f[i].y = (float)g_core.units[i].pos.y;

        g_pre_step_pos_f[i] = g_anim_pos_f[i];
    }

    memset(&g_p1_cmd, 0, sizeof(g_p1_cmd));
    memset(&g_p2_cmd, 0, sizeof(g_p2_cmd));
    g_p2_locked = false;

    g_exec_active = false;
    g_exec_stage  = EXE_NONE;
    g_exec_i      = 0;
    g_exec_n      = 0;

    g_act_pause_left = 0.0f;

    reset_p1_plan();

    {
        int idx = unit_index(TEAM_P1, SLOT_HERO);
        g_move_to = g_core.units[idx].pos;
    }

    bars_sync_to_real();

    g_inited = true;
}

// ===============================
//  P1プラン → TurnCmd
// ===============================
static void build_p1_cmd_from_plan(TurnCmd *out_cmd)
{
    out_cmd->cmd[SLOT_HERO] = (UnitCmd){ .has_move=false, .move_to={0,0}, .skill_index=-1, .target=-1 };
    out_cmd->cmd[SLOT_GIRL] = (UnitCmd){ .has_move=false, .move_to={0,0}, .skill_index=-1, .target=-1 };

    for (int s = 0; s < 2; s++) {
        UnitPlan *pl = &g_plan_p1[s];
        UnitCmd uc;
        uc.has_move    = pl->has_move;
        uc.move_to     = pl->move_to;
        uc.skill_index = pl->skill_index;
        uc.target      = pl->target;
        out_cmd->cmd[s] = uc;
    }
}

// ===============================
//  ST regen（char_defs 準拠）
//  ターン終了後、各ユニットの st_regen_per_turn だけ回復（上限はmax_st）
// ===============================
static void apply_st_regen_after_step(void)
{
    for (int i = 0; i < 4; i++) {
        Unit *u = &g_core.units[i];
        if (!u->alive) continue;

        const CharDef *cd = char_def_get(u->char_id);
        if (!cd) continue;

        int max_st = calc_unit_max_st(u);
        u->stats.st += cd->st_regen_per_turn;
        if (u->stats.st > max_st) u->stats.st = max_st;
        if (u->stats.st < 0) u->stats.st = 0;
    }
}

// ===============================
//  SPD順（演出用）
// ===============================
static void build_exec_order(void)
{
    battle_core_build_action_order(&g_core, g_exec_order, &g_exec_n);
    if (g_exec_n < 0) g_exec_n = 0;
    if (g_exec_n > 4) g_exec_n = 4;
}

static void start_exec_phase_from_cmds(void)
{
    for (int i = 0; i < 4; i++) {
        g_anim_pos_f[i] = g_pre_step_pos_f[i];

        g_cmd_has_move[i] = false;
        g_cmd_move_to[i]  = g_pre_step_pos[i];

        g_core.units[i].pos = g_pre_step_pos[i];
    }

    g_cmd_has_move[0] = g_p1_cmd.cmd[SLOT_HERO].has_move;
    g_cmd_move_to[0]  = g_p1_cmd.cmd[SLOT_HERO].move_to;
    g_cmd_has_move[1] = g_p1_cmd.cmd[SLOT_GIRL].has_move;
    g_cmd_move_to[1]  = g_p1_cmd.cmd[SLOT_GIRL].move_to;

    g_cmd_has_move[2] = g_p2_cmd.cmd[SLOT_HERO].has_move;
    g_cmd_move_to[2]  = g_p2_cmd.cmd[SLOT_HERO].move_to;
    g_cmd_has_move[3] = g_p2_cmd.cmd[SLOT_GIRL].has_move;
    g_cmd_move_to[3]  = g_p2_cmd.cmd[SLOT_GIRL].move_to;

    build_exec_order();

    g_exec_active = true;
    g_exec_stage  = EXE_MOVE;
    g_exec_i      = 0;

    g_act_pause_left = 0.0f;
}

// ===============================
//  ターン進行（ローカル）
// ===============================
static void try_advance_turn_local(void)
{
    if (!g_p1_locked || !g_p2_locked) return;
    if (g_exec_active) return;

    for (int i = 0; i < 4; i++) {
        g_pre_step_pos[i] = g_core.units[i].pos;
        g_pre_step_pos_f[i].x = (float)g_core.units[i].pos.x;
        g_pre_step_pos_f[i].y = (float)g_core.units[i].pos.y;
    }

    battle_core_submit_cmd(&g_core, TEAM_P1, &g_p1_cmd);
    battle_core_submit_cmd(&g_core, TEAM_P2, &g_p2_cmd);

    if (!battle_core_begin_exec(&g_core)) return;

    start_exec_phase_from_cmds();
}

// ===============================
//  実行フェーズ更新（SPD順）
// ===============================
static void exec_update(float dt)
{
    if (!g_exec_active) return;

    if (g_exec_i >= g_exec_n) {
        battle_core_end_exec(&g_core);

        // ★regenを char_defs に統一
        apply_st_regen_after_step();

        if (g_core.last_executed_skill_id && g_cutin.renderer) {
            const char *mp4 = battle_skill_movie_path(g_core.last_executed_skill_id);
            cutin_play_fullscreen_mpv(&g_cutin, mp4, 200, true);
        }

        memset(&g_p1_cmd, 0, sizeof(g_p1_cmd));
        memset(&g_p2_cmd, 0, sizeof(g_p2_cmd));
        g_p1_locked = false;
        g_p2_locked = false;

        g_exec_active = false;
        g_exec_stage = EXE_NONE;
        g_exec_i = 0;
        g_exec_n = 0;

        g_act_pause_left = 0.0f;

        reset_p1_plan();

        {
            int idx = unit_index(TEAM_P1, SLOT_HERO);
            g_move_to = g_core.units[idx].pos;
        }
        return;
    }

    int ui = g_exec_order[g_exec_i];
    Unit *u = &g_core.units[ui];

    if (!u->alive) {
        g_exec_i++;
        g_exec_stage = EXE_MOVE;
        g_act_pause_left = 0.0f;
        return;
    }

    if (g_exec_stage == EXE_MOVE) {
        if (!g_cmd_has_move[ui]) {
            g_exec_stage = EXE_ACT;
            g_act_pause_left = 0.0f;
            return;
        }

        SDL_FPoint cur = g_anim_pos_f[ui];
        SDL_FPoint dst = { (float)g_cmd_move_to[ui].x, (float)g_cmd_move_to[ui].y };

        float step = g_move_speed_cells * dt;

        float dx = dst.x - cur.x;
        float dy = dst.y - cur.y;

        if (dx > 0.0f) cur.x += (dx > step ? step : dx);
        if (dx < 0.0f) cur.x += (dx < -step ? -step : dx);
        if (dy > 0.0f) cur.y += (dy > step ? step : dy);
        if (dy < 0.0f) cur.y += (dy < -step ? -step : dy);

        g_anim_pos_f[ui] = cur;

        if (fabsf(dst.x - cur.x) < 0.001f && fabsf(dst.y - cur.y) < 0.001f) {
            g_anim_pos_f[ui] = dst;
            u->pos = g_cmd_move_to[ui];

            g_exec_stage = EXE_ACT;
            g_act_pause_left = 0.0f;
        }
        return;
    }

    if (g_exec_stage == EXE_ACT) {
        if (g_act_pause_left <= 0.0f) {
            battle_core_exec_act_for_unit(&g_core, ui);
            g_act_pause_left = g_act_pause_sec;
            return;
        }

        g_act_pause_left -= dt;
        if (g_act_pause_left > 0.0f) return;

        g_act_pause_left = 0.0f;
        g_exec_i++;
        g_exec_stage = EXE_MOVE;
        return;
    }
}

// ===================================
//  描画補助
// ===================================
static void set_color(SDL_Renderer *r, Uint8 R, Uint8 G, Uint8 B, Uint8 A)
{
    SDL_SetRenderDrawColor(r, R, G, B, A);
}

static const char* slot_label(Slot s)
{
    return (s == SLOT_HERO) ? "主人公" : "相棒";
}

static const char* ui_state_label(BattleUiState st)
{
    switch (st) {
    case UI_CMD_SELECT:   return "コマンド";
    case UI_MOVE_SELECT:  return "移動";
    case UI_SKILL_SELECT: return "技";
    case UI_TURN_CONFIRM: return "確認";
    default: return "?";
    }
}

static void draw_triangle_right(SDL_Renderer *r, int cx, int cy, int size,
                                Uint8 R, Uint8 G, Uint8 B, Uint8 A)
{
    int h = size / 2;

    SDL_BlendMode prev;
    SDL_GetRenderDrawBlendMode(r, &prev);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, R, G, B, A);

    for (int dy = -h; dy <= h; dy++) {
        int x1 = cx - h;
        int x2 = cx + h - abs(dy);
        SDL_RenderDrawLine(r, x1, cy + dy, x2, cy + dy);
    }

    SDL_SetRenderDrawBlendMode(r, prev);
}

static void draw_unit_ghost(SDL_Renderer *r, int origin_x, int origin_y, int cell,
                            const Pos p, Uint8 R, Uint8 G, Uint8 B, Uint8 A)
{
    int gx = (int)p.x, gy = (int)p.y;
    if (gx < 0 || gx >= GRID_W || gy < 0 || gy >= GRID_H) return;

    SDL_BlendMode prev;
    SDL_GetRenderDrawBlendMode(r, &prev);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    int px = origin_x + gx * cell;
    int py = origin_y + gy * cell;

    SDL_Rect rect = { px + 2, py + 2, cell - 4, cell - 4 };
    set_color(r, R, G, B, A);
    SDL_RenderFillRect(r, &rect);

    set_color(r, 255, 255, 255, A);
    SDL_RenderDrawRect(r, &rect);

    SDL_SetRenderDrawBlendMode(r, prev);
}

// ===============================
//  移動ハイライト
// ===============================
static void draw_move_highlight(SDL_Renderer *r,
                                int origin_x, int origin_y, int cell,
                                const Pos from, int mv)
{
    SDL_BlendMode prev;
    SDL_GetRenderDrawBlendMode(r, &prev);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    for (int y = 0; y < GRID_H; y++) {
        for (int x = 0; x < GRID_W; x++) {
            Pos p = {(int8_t)x, (int8_t)y};
            if (!is_move_in_range(from, p, mv)) continue;

            int px = origin_x + x * cell;
            int py = origin_y + y * cell;

            SDL_Rect rc = { px + 1, py + 1, cell - 2, cell - 2 };
            set_color(r, 240, 240, 240, 35);
            SDL_RenderFillRect(r, &rc);
        }
    }

    {
        int x = (int)from.x, y = (int)from.y;
        if (x >= 0 && x < GRID_W && y >= 0 && y < GRID_H) {
            int px = origin_x + x * cell;
            int py = origin_y + y * cell;
            SDL_Rect rc = { px + 1, py + 1, cell - 2, cell - 2 };
            set_color(r, 255, 255, 255, 60);
            SDL_RenderFillRect(r, &rc);
        }
    }

    SDL_SetRenderDrawBlendMode(r, prev);
}

static void draw_target_box(SDL_Renderer* r, int origin_x, int origin_y, int cell, Pos p)
{
    int gx = (int)p.x, gy = (int)p.y;
    if (gx < 0 || gx >= GRID_W || gy < 0 || gy >= GRID_H) return;

    int px = origin_x + gx * cell;
    int py = origin_y + gy * cell;

    SDL_Rect rc = { px + 1, py + 1, cell - 2, cell - 2 };

    SDL_BlendMode prev;
    SDL_GetRenderDrawBlendMode(r, &prev);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // 目立つ色（黄色系）＋少し透明
    SDL_SetRenderDrawColor(r, 255, 230, 80, 220);

    // 太枠っぽく3回描く（SDL2標準だけでできる）
    SDL_RenderDrawRect(r, &rc);
    rc.x++; rc.y++; rc.w -= 2; rc.h -= 2;
    SDL_RenderDrawRect(r, &rc);
    rc.x++; rc.y++; rc.w -= 2; rc.h -= 2;
    SDL_RenderDrawRect(r, &rc);

    SDL_SetRenderDrawBlendMode(r, prev);
}


// ===============================
//  グリッド＋ユニット描画
// ===============================
static void draw_battle_grid(SDL_Renderer *r, int origin_x, int origin_y, int cell, const BattleCore *b)
{
    set_color(r, 18, 18, 28, 255);
    SDL_Rect panel = { origin_x - 10, origin_y - 10, GRID_W * cell + 20, GRID_H * cell + 20 };
    SDL_RenderFillRect(r, &panel);

    if (g_ui == UI_MOVE_SELECT && !g_exec_active) {
        int idx = unit_index(TEAM_P1, g_act_slot);
        const Unit *u = &b->units[idx];
        Pos from = get_p1_unit_draw_pos(u);
        int mv = get_move_range_for_unit(u);
        draw_move_highlight(r, origin_x, origin_y, cell, from, mv);
    }

    set_color(r, 60, 60, 80, 255);
    for (int x = 0; x <= GRID_W; x++) {
        int px = origin_x + x * cell;
        SDL_RenderDrawLine(r, px, origin_y, px, origin_y + GRID_H * cell);
    }
    for (int y = 0; y <= GRID_H; y++) {
        int py = origin_y + y * cell;
        SDL_RenderDrawLine(r, origin_x, py, origin_x + GRID_W * cell, py);
    }

    if (!g_exec_active && !g_p1_locked) {
        if (g_preview_active[SLOT_HERO]) {
            draw_unit_ghost(r, origin_x, origin_y, cell, g_preview_pos[SLOT_HERO],
                            40, 120, 255, 110);
        }
        if (g_preview_active[SLOT_GIRL]) {
            draw_unit_ghost(r, origin_x, origin_y, cell, g_preview_pos[SLOT_GIRL],
                            80, 220, 255, 110);
        }
    }

    for (int i = 0; i < 4; i++) {
        const Unit *u = &b->units[i];
        if (!u->alive) continue;

        int gx, gy;
        if (g_exec_active) {
            gx = (int)roundf(g_anim_pos_f[i].x);
            gy = (int)roundf(g_anim_pos_f[i].y);
        } else {
            gx = (int)u->pos.x;
            gy = (int)u->pos.y;
        }

        if (gx < 0 || gx >= GRID_W || gy < 0 || gy >= GRID_H) continue;

        Uint8 R=255,G=255,Bc=255;
        if (u->team == TEAM_P1 && u->slot == SLOT_HERO) { R=40;  G=120; Bc=255; }
        if (u->team == TEAM_P1 && u->slot == SLOT_GIRL) { R=80;  G=220; Bc=255; }
        if (u->team == TEAM_P2 && u->slot == SLOT_HERO) { R=255; G=70;  Bc=70;  }
        if (u->team == TEAM_P2 && u->slot == SLOT_GIRL) { R=255; G=100; Bc=200; }

        int px = origin_x + gx * cell;
        int py = origin_y + gy * cell;

        SDL_Rect rect = { px + 2, py + 2, cell - 4, cell - 4 };
        set_color(r, R, G, Bc, 255);
        SDL_RenderFillRect(r, &rect);

        bool highlight = false;
        if (!g_exec_active) {
            highlight = (u->team == TEAM_P1 && u->slot == g_act_slot);
        } else {
            if (g_exec_i < g_exec_n) {
                int cur_ui = g_exec_order[g_exec_i];
                highlight = (cur_ui == i);
            }
        }

        if (highlight) set_color(r, 255, 255, 255, 255);
        else set_color(r, 10, 10, 10, 255);
        SDL_RenderDrawRect(r, &rect);
    }

    if (g_ui == UI_MOVE_SELECT && !g_exec_active) {
        int gx = (int)g_move_to.x;
        int gy = (int)g_move_to.y;
        int px = origin_x + gx * cell;
        int py = origin_y + gy * cell;
        SDL_Rect cursor = { px + 1, py + 1, cell - 2, cell - 2 };
        set_color(r, 255, 255, 255, 255);
        SDL_RenderDrawRect(r, &cursor);
    }

    // --- 追加：ターゲットをマップ上でハイライト ---
if (!g_exec_active && !g_p1_locked && g_ui == UI_SKILL_SELECT) {
    Team enemy = TEAM_P2;
    Slot ts = (g_target == 1) ? SLOT_GIRL : SLOT_HERO;
    int tidx = unit_index(enemy, ts);
    const Unit* tgt = &b->units[tidx];
    if (tgt->alive) {
        draw_target_box(r, origin_x, origin_y, cell, tgt->pos);
    }
}

}

// ===============================
//  HP/ST パネル
// ===============================
static void draw_bar(SDL_Renderer *r, int x, int y, int w, int h, int cur, int maxv)
{
    set_color(r, 40, 40, 55, 255);
    SDL_Rect frame = {x, y, w, h};
    SDL_RenderFillRect(r, &frame);
    set_color(r, 80, 80, 110, 255);
    SDL_RenderDrawRect(r, &frame);

    if (maxv < 1) maxv = 1;
    if (cur < 0) cur = 0;
    if (cur > maxv) cur = maxv;

    int fill_w = (int)((double)w * ((double)cur / (double)maxv));
    if (fill_w < 0) fill_w = 0;

    set_color(r, 90, 200, 120, 255);
    SDL_Rect fill = {x + 1, y + 1, (fill_w > 2 ? fill_w - 2 : 0), h - 2};
    SDL_RenderFillRect(r, &fill);
}

static void draw_stat_panel(SDL_Renderer *r, int x, int y, int w, int h,
                            const char *name,
                            int hp, int hp_max,
                            int st, int st_max)
{
    set_color(r, 20, 20, 30, 255);
    SDL_Rect panel = {x, y, w, h};
    SDL_RenderFillRect(r, &panel);
    set_color(r, 60, 60, 80, 255);
    SDL_RenderDrawRect(r, &panel);

    ui_text_draw(r, g_font, name, x + 10, y + 6);

    ui_text_draw(r, g_font, "HP", x + 10, y + 34);
    draw_bar(r, x + 50, y + 38, w - 60, 14, hp, hp_max);

    ui_text_draw(r, g_font, "ST", x + 10, y + 58);
    draw_bar(r, x + 50, y + 62, w - 60, 14, st, st_max);

    char buf[64];
    snprintf(buf, sizeof(buf), "%d/%d", hp, hp_max);
    ui_text_draw(r, g_font, buf, x + w - 120, y + 30);

    snprintf(buf, sizeof(buf), "%d/%d", st, st_max);
    ui_text_draw(r, g_font, buf, x + w - 120, y + 54);
}

// ===============================
//  Scene I/F
// ===============================
void scene_battle_enter(void)
{
    init_battle_core();
}

void scene_battle_leave(void)
{
}

// 決定処理（待機）
static void p1_finalize_current_unit_wait(void)
{
    UnitPlan *pl = &g_plan_p1[g_act_slot];
    pl->decided = true;
    pl->has_move = true;
    pl->move_to = g_move_to;
    pl->skill_index = -1;
    pl->target = -1;

    g_preview_active[g_act_slot] = true;
    g_preview_pos[g_act_slot] = g_move_to;

    if (g_act_slot == SLOT_HERO) {
        g_act_slot = SLOT_GIRL;
        g_ui = UI_CMD_SELECT;
        g_cmd = CMD_ATTACK;

        int idx = unit_index(TEAM_P1, SLOT_GIRL);
        g_move_to = g_core.units[idx].pos;
    } else {
        g_ui = UI_TURN_CONFIRM;
        g_confirm = CONFIRM_YES;
    }
}

// 決定処理（攻撃）
static void p1_finalize_current_unit_attack(void)
{
    UnitPlan *pl = &g_plan_p1[g_act_slot];
    pl->decided = true;
    pl->has_move = true;
    pl->move_to = g_move_to;
    pl->skill_index = (int8_t)g_skill_index[g_act_slot];
    pl->target = (int8_t)g_target;

    g_preview_active[g_act_slot] = true;
    g_preview_pos[g_act_slot] = g_move_to;

    if (g_act_slot == SLOT_HERO) {
        g_act_slot = SLOT_GIRL;
        g_ui = UI_CMD_SELECT;
        g_cmd = CMD_ATTACK;

        int idx = unit_index(TEAM_P1, SLOT_GIRL);
        g_move_to = g_core.units[idx].pos;
    } else {
        g_ui = UI_TURN_CONFIRM;
        g_confirm = CONFIRM_YES;
    }
}

void scene_battle_update(float dt)
{
    if (!g_inited) init_battle_core();

    bars_update(dt);

    // Esc: 強制終了（いつでもHOMEへ）
    if (input_is_pressed(SDL_SCANCODE_ESCAPE)) {
        change_scene(SCENE_HOME);
        return;
    }

    if (g_core.phase == BPHASE_END) {
        if (input_is_pressed(SDL_SCANCODE_RETURN)) change_scene(SCENE_HOME);
        return;
    }

    // 実行フェーズ中は入力を受け付けず、演出だけ進める
    if (g_exec_active) {
        exec_update(dt);
        return;
    }

    // Q：1手戻し（確定前のみ）
    if (!g_p1_locked && input_is_pressed(SDL_SCANCODE_Q)) {
        undo_pop();
        return;
    }

    // P1入力（主人公→相棒の順）
    if (!g_p1_locked) {

        if (g_ui == UI_CMD_SELECT) {
            if (input_is_pressed(SDL_SCANCODE_LEFT) || input_is_pressed(SDL_SCANCODE_RIGHT)) {
                g_cmd = (g_cmd == CMD_ATTACK) ? CMD_WAIT : CMD_ATTACK;
            }

            if (input_is_pressed(SDL_SCANCODE_RETURN)) {
                undo_push();

                int idx = unit_index(TEAM_P1, g_act_slot);
                g_move_to = g_core.units[idx].pos;
                g_ui = UI_MOVE_SELECT;
            }
        }
        else if (g_ui == UI_MOVE_SELECT) {
            if (input_is_pressed(SDL_SCANCODE_UP))    g_move_to.y--;
            if (input_is_pressed(SDL_SCANCODE_DOWN))  g_move_to.y++;
            if (input_is_pressed(SDL_SCANCODE_LEFT))  g_move_to.x--;
            if (input_is_pressed(SDL_SCANCODE_RIGHT)) g_move_to.x++;

            clamp_move_cursor(&g_move_to);

            if (input_is_pressed(SDL_SCANCODE_RETURN)) {
                int idx = unit_index(TEAM_P1, g_act_slot);
                const Unit *u = &g_core.units[idx];

                Pos from = u->pos;
                int mv = get_move_range_for_unit(u);

                if (!is_move_in_range(from, g_move_to, mv)) return;

                undo_push();

                if (g_cmd == CMD_ATTACK) g_ui = UI_SKILL_SELECT;
                else p1_finalize_current_unit_wait();
            }
        }
        else if (g_ui == UI_SKILL_SELECT) {
            int idx = unit_index(TEAM_P1, g_act_slot);
            const Unit *u = &g_core.units[idx];

            // ★char_defs に合わせた技数
            int max_skill = get_skill_count_for_unit(u);
            if (max_skill < 1) max_skill = 1;

            if (input_is_pressed(SDL_SCANCODE_LEFT)) {
                g_skill_index[g_act_slot]--;
                if (g_skill_index[g_act_slot] < 0) g_skill_index[g_act_slot] = 0;
            }
            if (input_is_pressed(SDL_SCANCODE_RIGHT)) {
                g_skill_index[g_act_slot]++;
                if (g_skill_index[g_act_slot] > (max_skill - 1)) g_skill_index[g_act_slot] = (max_skill - 1);
            }

            if (input_is_pressed(SDL_SCANCODE_T)) g_target = 1 - g_target;

            if (input_is_pressed(SDL_SCANCODE_RETURN)) {
                // 念のためクランプ
                if (g_skill_index[g_act_slot] < 0) g_skill_index[g_act_slot] = 0;
                if (g_skill_index[g_act_slot] > (max_skill - 1)) g_skill_index[g_act_slot] = (max_skill - 1);

                undo_push();
                p1_finalize_current_unit_attack();
            }
        }
        else if (g_ui == UI_TURN_CONFIRM) {
            if (input_is_pressed(SDL_SCANCODE_LEFT) || input_is_pressed(SDL_SCANCODE_UP)) {
                g_confirm = CONFIRM_YES;
            }
            if (input_is_pressed(SDL_SCANCODE_RIGHT) || input_is_pressed(SDL_SCANCODE_DOWN)) {
                g_confirm = CONFIRM_NO;
            }

            if (input_is_pressed(SDL_SCANCODE_RETURN)) {
                if (g_confirm == CONFIRM_YES) {
                    undo_push();
                    build_p1_cmd_from_plan(&g_p1_cmd);
                    g_p1_locked = true;
                    g_ui = UI_CMD_SELECT;
                } else {
                    undo_push();
                    g_ui = UI_CMD_SELECT;
                    g_act_slot = SLOT_GIRL;
                    g_cmd = CMD_ATTACK;

                    int idx = unit_index(TEAM_P1, SLOT_GIRL);
                    g_move_to = g_core.units[idx].pos;
                }
            }
        }
    }

    // P2側：ローカル仮（オンライン時は受信へ）
    if (!g_p2_locked) {
        g_p2_cmd.cmd[SLOT_HERO] = (UnitCmd){ .has_move=true,  .move_to=g_core.units[2].pos, .skill_index=0,  .target=0 };
        g_p2_cmd.cmd[SLOT_GIRL] = (UnitCmd){ .has_move=true,  .move_to=g_core.units[3].pos, .skill_index=-1, .target=-1 };
        g_p2_locked = true;
    }

    try_advance_turn_local();
}

void scene_battle_render(SDL_Renderer *r)
{
    g_cutin.renderer = r;
    g_cutin.screen_w = 1280;
    g_cutin.screen_h = 720;

    SDL_SetRenderDrawColor(r, 10, 10, 16, 255);
    SDL_RenderClear(r);

    if (!g_font) g_font = ui_load_font("assets/font/main.otf", 28);

    // ===============================
    // TopBar
    // ===============================
    {
        set_color(r, 15, 15, 24, 255);
        SDL_Rect top = {0, 0, 1280, 72};
        SDL_RenderFillRect(r, &top);
        set_color(r, 60, 60, 80, 255);
        SDL_RenderDrawRect(r, &top);

        char buf[128];
        snprintf(buf, sizeof(buf), "TURN %d", g_core.turn);
        ui_text_draw(r, g_font, buf, 40, 20);

        if (g_exec_active) {
            ui_text_draw(r, g_font, "実行中（SPD順）", 240, 20);
        } else if (!g_p1_locked) {
            char st[256];
            snprintf(st, sizeof(st), "入力: %s  状態: %s",
                     slot_label(g_act_slot), ui_state_label(g_ui));
            ui_text_draw(r, g_font, st, 240, 20);
        } else {
            ui_text_draw(r, g_font, "P1 入力完了", 240, 20);
        }
    }

    // ===============================
    // ステータスUI（2x2）: 1P上 / 2P下
    // ===============================
    {
        const Unit *p1h = &g_core.units[0];
        const Unit *p1g = &g_core.units[1];
        const Unit *p2h = &g_core.units[2];
        const Unit *p2g = &g_core.units[3];

        int px = 620;
        int py = 90;
        int pw = 300;
        int ph = 88;
        int gap = 12;

        draw_stat_panel(r, px,            py,            pw, ph, "1P主人公",
                        (int)lroundf(g_disp_hp[0]), calc_unit_max_hp(p1h),
                        (int)lroundf(g_disp_st[0]), calc_unit_max_st(p1h));
        draw_stat_panel(r, px + pw + gap, py,            pw, ph, "1P相棒",
                        (int)lroundf(g_disp_hp[1]), calc_unit_max_hp(p1g),
                        (int)lroundf(g_disp_st[1]), calc_unit_max_st(p1g));

        draw_stat_panel(r, px,            py + ph + gap, pw, ph, "2P主人公",
                        (int)lroundf(g_disp_hp[2]), calc_unit_max_hp(p2h),
                        (int)lroundf(g_disp_st[2]), calc_unit_max_st(p2h));
        draw_stat_panel(r, px + pw + gap, py + ph + gap, pw, ph, "2P相棒",
                        (int)lroundf(g_disp_hp[3]), calc_unit_max_hp(p2g),
                        (int)lroundf(g_disp_st[3]), calc_unit_max_st(p2g));
    }

    // ===============================
    // マップ
    // ===============================
    const int map_origin_x = 60;
    const int map_origin_y = 110;
    const int cell = 24;
    draw_battle_grid(r, map_origin_x, map_origin_y, cell, &g_core);

    // ===============================
    // CommandBar / SkillBar / Confirm
    // ===============================
    {
        const int bar_x = 0;
        const int bar_y = 640;
        const int bar_w = 1280;
        const int bar_h = 80;

        set_color(r, 15, 15, 24, 255);
        SDL_Rect bar = {bar_x, bar_y, bar_w, bar_h};
        SDL_RenderFillRect(r, &bar);
        set_color(r, 60, 60, 80, 255);
        SDL_RenderDrawRect(r, &bar);

        int y = 664;

        if (g_exec_active) {
            ui_text_draw(r, g_font, "SPD順に実行中…（Esc:強制終了）", 80, 692);
        }
        else if (!g_p1_locked && g_ui == UI_TURN_CONFIRM) {
            ui_text_draw(r, g_font, "決定？", 80, 660);

            const int margin = 24;

            SDL_Rect yes_box = { 240, bar_y + 16, 180, 40 };

            SDL_Rect no_box = {
                bar_x + bar_w - margin - 220,
                bar_y + bar_h - margin - 40,
                220, 40
            };

            set_color(r, 30, 30, 45, 255);
            SDL_RenderFillRect(r, &yes_box);
            SDL_RenderFillRect(r, &no_box);

            if (g_confirm == CONFIRM_YES) {
                set_color(r, 55, 55, 85, 255);
                SDL_RenderFillRect(r, &yes_box);
            } else {
                set_color(r, 55, 55, 85, 255);
                SDL_RenderFillRect(r, &no_box);
            }

            set_color(r, 110, 110, 150, 255);
            SDL_RenderDrawRect(r, &yes_box);
            SDL_RenderDrawRect(r, &no_box);

            ui_text_draw(r, g_font, "はい",   yes_box.x + 70, yes_box.y + 8);
            ui_text_draw(r, g_font, "いいえ", no_box.x + 70,  no_box.y + 8);

            ui_text_draw(r, g_font, "←↑:はい  →↓:いいえ  Enter:決定  Q:1手戻し  Esc:強制終了", 80, 692);
        }
        else if (!g_p1_locked && g_ui == UI_SKILL_SELECT) {
            int idx = unit_index(TEAM_P1, g_act_slot);
            const Unit *u = &g_core.units[idx];

            int count = get_skill_count_for_unit(u);
            if (count < 1) count = 1;

            int base_x = 140;
            int gap = 24;
            int box_w = 180;
            int box_h = 34;

            int sidx = g_skill_index[g_act_slot];
            if (sidx < 0) sidx = 0;
            if (sidx > (count - 1)) sidx = (count - 1);

            for (int i = 0; i < count; i++) {
                int bx = base_x + i * (box_w + gap);
                SDL_Rect box = { bx, y - 6, box_w, box_h };

                set_color(r, 30, 30, 45, 255);
                SDL_RenderFillRect(r, &box);

                if (i == sidx) {
                    set_color(r, 55, 55, 85, 255);
                    SDL_RenderFillRect(r, &box);
                }

                set_color(r, 110, 110, 150, 255);
                SDL_RenderDrawRect(r, &box);

                char label[32];
                snprintf(label, sizeof(label), "技%d", i + 1);
                ui_text_draw(r, g_font, label, bx + 60, y);

                if (i == sidx) {
                    draw_triangle_right(r, bx - 14, y + 14, 20, 255, 60, 60, 230);
                }
            }

            ui_text_draw(r, g_font, "←→:技選択  T:ターゲット  Enter:確定  Q:1手戻し  Esc:強制終了", 80, 692);
        }
        else {
            int attack_x = 140;
            int wait_x   = 360;

            SDL_Rect atk_box  = { attack_x - 20, y - 6, 160, 34 };
            SDL_Rect wait_box = { wait_x   - 20, y - 6, 160, 34 };

            set_color(r, 30, 30, 45, 255);
            SDL_RenderFillRect(r, &atk_box);
            SDL_RenderFillRect(r, &wait_box);

            if (g_cmd == CMD_ATTACK) {
                set_color(r, 55, 55, 85, 255);
                SDL_RenderFillRect(r, &atk_box);
            } else {
                set_color(r, 55, 55, 85, 255);
                SDL_RenderFillRect(r, &wait_box);
            }

            set_color(r, 110, 110, 150, 255);
            SDL_RenderDrawRect(r, &atk_box);
            SDL_RenderDrawRect(r, &wait_box);

            ui_text_draw(r, g_font, "攻撃", attack_x + 20, y);
            ui_text_draw(r, g_font, "待機", wait_x + 20, y);

            int tri_y = y + 14;
            if (g_cmd == CMD_ATTACK) {
                draw_triangle_right(r, attack_x - 10, tri_y, 20, 255, 60, 60, 230);
            } else {
                draw_triangle_right(r, wait_x - 10,   tri_y, 20, 255, 60, 60, 230);
            }

            if (!g_p1_locked) {
                if (g_ui == UI_CMD_SELECT) {
                    ui_text_draw(r, g_font, "←→:攻撃/待機  Enter:決定(移動へ)  Q:1手戻し  Esc:強制終了", 80, 692);
                } else if (g_ui == UI_MOVE_SELECT) {
                    char buf[256];
                    int idx = unit_index(TEAM_P1, g_act_slot);
                    const Unit *u = &g_core.units[idx];
                    snprintf(buf, sizeof(buf), "矢印:移動先  Enter:確定(範囲=%d)  Q:1手戻し  Esc:強制終了",
                             get_move_range_for_unit(u));
                    ui_text_draw(r, g_font, buf, 80, 692);
                } else {
                    ui_text_draw(r, g_font, "Enter:確定  Q:1手戻し  Esc:強制終了", 80, 692);
                }
            } else {
                ui_text_draw(r, g_font, "P1入力完了 → ターン進行", 80, 692);
            }
        }
    }

    SDL_RenderPresent(r);
}
