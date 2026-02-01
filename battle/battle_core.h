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

typedef struct {
    BattlePhase phase;
    int turn;
    Unit units[4];

    // 「成立した最初の技だけ」演出に使う（実skill_id）
    const char *last_executed_skill_id;

    TurnCmd _pending_cmd[2];
    bool    _has_cmd[2];

    char p1_girl_id[32];
    char p2_girl_id[32];
    bool p1_tag;
    bool p2_tag;

    // --- 追加：実行フェーズ用 ---
    bool _exec_active;

    // --- 追加：カウンター状態（Unitを汚さない） ---
    bool counter_ready[4];   // 構え中か
    int  counter_power[4];   // 反撃の power（atk+power）
    int  counter_range[4];   // 反撃射程（マンハッタン）
} BattleCore;

bool battle_core_init(
    BattleCore *b,
    const char *p1_girl_id, bool p1_tag, Stats p1_hero, Stats p1_girl,
    const char *p2_girl_id, bool p2_tag, Stats p2_hero, Stats p2_girl
);

void battle_core_submit_cmd(BattleCore *b, Team team, const TurnCmd *cmd);

// 互換のため残す（旧：一括確定）
bool battle_core_step(BattleCore *b);

// --- 新：段階実行API ---
bool battle_core_begin_exec(BattleCore *b);
void battle_core_exec_act_for_unit(BattleCore *b, int ui);
void battle_core_end_exec(BattleCore *b);

void battle_core_build_action_order(const BattleCore *b, int out_idx[4], int *out_n);

#ifdef __cplusplus
}
#endif
