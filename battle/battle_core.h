// battle/battle_core.h
#pragma once
#include <stdbool.h>
#include <stdint.h>

#include "battle_types.h"
#include "battle_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BPHASE_INPUT = 0,
    BPHASE_RESOLVE,
    BPHASE_END
} BattlePhase;

// ===============================
//  演出/適用用イベント
//   - 「技が成立した瞬間」に ANIM を出す
//   - 効果（HP変動）は EFFECT_* を後から apply する
// ===============================
typedef enum {
    BEV_NONE = 0,
    BEV_ANIM_SKILL,     // mp4再生のトリガ（この時点ではHPは変えない）
    BEV_EFFECT_DAMAGE,  // 対象にダメージ
    BEV_EFFECT_HEAL     // 対象を回復
} BattleEventType;

typedef struct {
    BattleEventType type;

    // 誰が出した演出/効果か（unit index: 0..3）
    int actor_ui;

    // 対象（単体用）。不要なら -1
    int target_ui;

    // AOE用（ANIM_SKILLで使う想定）
    Pos center;
    int radius;

    // EFFECT_* の量（damage/heal）
    int value;

    // ANIM_SKILL の識別子（skill_id）。EFFECT_* は基本NULLでOK
    const char *skill_id;
} BattleEvent;

#define BATTLE_EVENT_MAX 32

typedef struct {
    BattlePhase phase;
    int turn;
    Unit units[4];
    int  hp_max[4];      // 最大HP（初期値を最大として保持）

    // （互換）scene側が参照しているなら維持
    const char *last_executed_skill_id;

    TurnCmd _pending_cmd[2];
    bool    _has_cmd[2];

    char p1_girl_id[32];
    char p2_girl_id[32];
    bool p1_tag;
    bool p2_tag;

    // --- 実行フェーズ用 ---
    bool _exec_active;

    // --- カウンター状態（Unitを汚さない） ---
    bool        counter_ready[4];     // 構え中か
    int         counter_range[4];     // 反撃射程（マンハッタン）
    const char *counter_skill_id[4];  // 反撃時に流す演出（skill_id）

    // --- 新：イベントキュー（直近の1アクション分） ---
    BattleEvent events[BATTLE_EVENT_MAX];
    int ev_count;
} BattleCore;

bool battle_core_init(
    BattleCore *b,
    const char *p1_girl_id, bool p1_tag, Stats p1_hero, Stats p1_girl,
    const char *p2_girl_id, bool p2_tag, Stats p2_hero, Stats p2_girl
);

void battle_core_submit_cmd(BattleCore *b, Team team, const TurnCmd *cmd);

// 互換のため残す（旧：一括確定）
bool battle_core_step(BattleCore *b);

// --- 段階実行API ---
bool battle_core_begin_exec(BattleCore *b);
void battle_core_exec_act_for_unit(BattleCore *b, int ui);
void battle_core_end_exec(BattleCore *b);

void battle_core_build_action_order(const BattleCore *b, int out_idx[4], int *out_n);

// --- 新：イベントAPI ---
void battle_core_clear_events(BattleCore *b);
int  battle_core_event_count(const BattleCore *b);
const BattleEvent* battle_core_get_event(const BattleCore *b, int idx);

// イベントの効果を反映（ANIM_SKILLは何もしない）
void battle_core_apply_event(BattleCore *b, const BattleEvent *ev);

// 現在キューに溜まっているイベントをすべて適用してクリア
void battle_core_apply_events(BattleCore *b);

#ifdef __cplusplus
}
#endif
