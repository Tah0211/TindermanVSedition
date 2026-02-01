// battle/battle_core.c
#include "battle_core.h"
#include <string.h>
#include <stdio.h>

#include "battle_skills.h"  // battle_skill_get()
#include "char_defs.h"      // char_def_get(), char_def_get_skill_id_at()

#define MAP_MIN 0
#define MAP_MAX 20

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static bool team_all_dead(const BattleCore *b, Team t) {
    int i0 = unit_index(t, SLOT_HERO);
    int i1 = unit_index(t, SLOT_GIRL);
    return (!b->units[i0].alive) && (!b->units[i1].alive);
}

static bool is_tag_learned_for_team(const BattleCore *b, Team t) {
    return (t == TEAM_P1) ? b->p1_tag : b->p2_tag;
}

static bool spend_st_if_possible(Unit *u, int cost) {
    if (!u) return false;
    if (cost <= 0) return true;
    if (u->stats.st < cost) return false;
    u->stats.st -= cost;
    if (u->stats.st < 0) u->stats.st = 0;
    return true;
}

static int calc_atk_plus_power(const Unit *att, int power) {
    int v = 1;
    if (att) v = att->stats.atk;
    v += power;
    if (v < 1) v = 1;
    return v;
}

static bool in_range_manhattan_pos(Pos a, Pos b, int range) {
    if (range < 0) return false;
    return manhattan(a, b) <= range;
}

static bool in_range_manhattan_units(const Unit *a, const Unit *t, int range) {
    if (!a || !t) return false;
    return in_range_manhattan_pos(a->pos, t->pos, range);
}

static void apply_damage(Unit *tgt, int dmg) {
    if (!tgt || !tgt->alive) return;
    tgt->stats.hp -= dmg;
    if (tgt->stats.hp <= 0) {
        tgt->stats.hp = 0;
        tgt->alive = false;
    }
}

static void apply_heal(Unit *tgt, int amount) {
    if (!tgt || !tgt->alive) return;
    if (amount < 1) amount = 1;
    tgt->stats.hp += amount;
    // max_hp がまだ無いので上限クランプはしない
}

static void apply_aoe_mixed(BattleCore *b, Team actor_team, Pos center, int dmg, int radius) {
    if (!b) return;
    if (radius <= 0) radius = 1;

    Team enemy = (actor_team == TEAM_P1) ? TEAM_P2 : TEAM_P1;

    for (int i = 0; i < 4; i++) {
        Unit *u = &b->units[i];
        if (!u->alive) continue;
        if (manhattan(u->pos, center) > radius) continue;

        if (u->team == enemy) {
            apply_damage(u, dmg);
        } else {
            int half = dmg / 2; // 切り捨て
            if (half < 1) half = 1;
            apply_damage(u, half);
        }
    }
}

static const char* resolve_skill_id_for_actor(const BattleCore *b, const Unit *actor, int skill_index) {
    if (!b || !actor) return NULL;
    if (skill_index < 0) return NULL;

    const CharDef *cd = char_def_get(actor->char_id);
    if (!cd) return NULL;

    bool tag = is_tag_learned_for_team(b, actor->team);
    return char_def_get_skill_id_at(cd, tag, skill_index);
}

static void apply_st_regen_end_of_turn(BattleCore *b) {
    if (!b) return;
    for (int i = 0; i < 4; i++) {
        Unit *u = &b->units[i];
        if (!u->alive) continue;

        const CharDef *cd = char_def_get(u->char_id);
        if (!cd) continue;

        u->stats.st += cd->st_regen_per_turn;
        if (u->stats.st < 0) u->stats.st = 0;
        // max_st がまだ無いなら上限クランプはしない
    }
}

// --- 新：SkillType 対応（ATTACK/HEAL/COUNTER） ---
static void do_skill(BattleCore *b, Team actor_team, Slot actor_slot, const UnitCmd *uc) {
    int aidx = unit_index(actor_team, actor_slot);
    Unit *att = &b->units[aidx];
    if (!att->alive) return;

    // wait
    if (uc->skill_index < 0) return;

    // skill_index -> skill_id（キャラ辞書）
    const char *skill_id = resolve_skill_id_for_actor(b, att, (int)uc->skill_index);
    if (!skill_id) return;

    // skill_id -> SkillDef（技辞書）
    const SkillDef *sk = battle_skill_get(skill_id);
    if (!sk) return;

    // 以後「成立条件」を満たしたときだけ ST 消費する方針
    // COUNTERは「構え時に消費」なので、成立条件＝構え成立

    // -------------------------
    // COUNTER：対象不要（自分に状態付与）
    // -------------------------
    if (sk->type == SKTYPE_COUNTER) {
        // ST不足なら不発
        if (!spend_st_if_possible(att, sk->st_cost)) return;

        b->counter_ready[aidx] = true;
        b->counter_power[aidx] = sk->power;
        b->counter_range[aidx] = sk->range;

        if (!b->last_executed_skill_id) b->last_executed_skill_id = skill_id;
        return;
    }

    // target slot は 0=hero, 1=girl を共通利用
    Slot ts = (uc->target == 1) ? SLOT_GIRL : SLOT_HERO;

    // -------------------------
    // HEAL：味方を対象（targetは味方hero/girl）
    // -------------------------
    if (sk->type == SKTYPE_HEAL) {
        int tidx = unit_index(actor_team, ts);
        Unit *tgt = &b->units[tidx];
        if (!tgt->alive) return;

        // range check
        // 範囲攻撃は「射程なし」なので判定しない
        if (sk->target == SKT_SINGLE) {
            if (!in_range_manhattan_units(att, tgt, sk->range)) return;
        }

        // ST不足なら不発（成立時のみ消費）
        if (!spend_st_if_possible(att, sk->st_cost)) return;

        int heal = sk->power;
        if (heal < 1) heal = 1;
        apply_heal(tgt, heal);

        if (!b->last_executed_skill_id) b->last_executed_skill_id = skill_id;
        return;
    }

    // -------------------------
    // ATTACK：敵を対象（targetは敵hero/girl）
    // -------------------------
    if (sk->type == SKTYPE_ATTACK) {
        Team enemy = (actor_team == TEAM_P1) ? TEAM_P2 : TEAM_P1;
        Unit *tgt = NULL;
        int tidx = -1;

        if (sk->target == SKT_SINGLE) {
            tidx = unit_index(enemy, ts);
            tgt = &b->units[tidx];
            if (!tgt->alive) return;
        }
// range check（成立条件）
        // 範囲攻撃は「射程なし」なので判定しない
        if (sk->target == SKT_SINGLE) {
            if (!in_range_manhattan_units(att, tgt, sk->range)) return;
        }

        // ST不足なら不発（成立時のみ消費）
        if (!spend_st_if_possible(att, sk->st_cost)) return;

        int dmg = calc_atk_plus_power(att, sk->power);

        // 反撃フラグを先に見ておく（攻撃を受けた瞬間に発動判定したい）
        // 反撃フラグ（単体攻撃のときだけ参照）
        bool had_counter = false;
        int  crange = 0;
        int  cpower = 0;
        if (sk->target == SKT_SINGLE && tidx >= 0) {
            had_counter = b->counter_ready[tidx];
            crange = b->counter_range[tidx];
            cpower = b->counter_power[tidx];
        }

        if (sk->target == SKT_SINGLE) {
            apply_damage(tgt, dmg);
        } else {
            // 範囲攻撃：中心はコマンド側で指定（射程判定なし）
            Pos c = uc->center;
            c.x = (int8_t)clampi((int)c.x, 0, 20);
            c.y = (int8_t)clampi((int)c.y, 0, 20);

            int r = (sk->aoe_radius <= 0) ? 1 : sk->aoe_radius;
            apply_aoe_mixed(b, actor_team, c, dmg, r);
        }

        // カウンター：単体攻撃に対してのみ発動（AOEは一旦不発にして読み合いを単純化）
        // ※必要なら「AOEでも中心対象が被弾したら発動」等に後で拡張
        if (had_counter && sk->target == SKT_SINGLE && tgt) {
            // 構えは消費（発動してもしなくても解除する設計）
            b->counter_ready[tidx] = false;

            // 反撃側（tgt）が生きていて、射程内なら反撃
            if (tgt->alive && att->alive && in_range_manhattan_pos(tgt->pos, att->pos, crange)) {
                int cdmg = calc_atk_plus_power(tgt, cpower);
                apply_damage(att, cdmg);
            }
        }

        if (!b->last_executed_skill_id) b->last_executed_skill_id = skill_id;
        return;
    }

    // 未知 type は不発
}

static void apply_move_if_any(BattleCore *b, Team t, Slot s, const UnitCmd *uc) {
    if (!uc->has_move) return;

    int idx = unit_index(t, s);
    Unit *u = &b->units[idx];
    if (!u->alive) return;

    int nx = clampi((int)uc->move_to.x, MAP_MIN, MAP_MAX);
    int ny = clampi((int)uc->move_to.y, MAP_MIN, MAP_MAX);
    u->pos.x = (int8_t)nx;
    u->pos.y = (int8_t)ny;
}

bool battle_core_init(
    BattleCore *b,
    const char *p1_girl_id, bool p1_tag, Stats p1_hero, Stats p1_girl,
    const char *p2_girl_id, bool p2_tag, Stats p2_hero, Stats p2_girl
) {
    if (!b) return false;
    memset(b, 0, sizeof(*b));

    b->phase = BPHASE_INPUT;
    b->turn = 1;
    b->last_executed_skill_id = NULL;
    b->_exec_active = false;

    if (p1_girl_id) snprintf(b->p1_girl_id, sizeof(b->p1_girl_id), "%s", p1_girl_id);
    if (p2_girl_id) snprintf(b->p2_girl_id, sizeof(b->p2_girl_id), "%s", p2_girl_id);
    b->p1_tag = p1_tag;
    b->p2_tag = p2_tag;

    // 初期配置（0..20）
    b->units[unit_index(TEAM_P1, SLOT_HERO)] = (Unit){
        .alive=true, .team=TEAM_P1, .slot=SLOT_HERO, .char_id="hero",
        .pos=(Pos){2,10}, .stats=p1_hero
    };
    b->units[unit_index(TEAM_P1, SLOT_GIRL)] = (Unit){
        .alive=true, .team=TEAM_P1, .slot=SLOT_GIRL,
        .char_id=(b->p1_girl_id[0] ? b->p1_girl_id : "himari"),
        .pos=(Pos){2,12}, .stats=p1_girl
    };
    b->units[unit_index(TEAM_P2, SLOT_HERO)] = (Unit){
        .alive=true, .team=TEAM_P2, .slot=SLOT_HERO, .char_id="hero",
        .pos=(Pos){18,10}, .stats=p2_hero
    };
    b->units[unit_index(TEAM_P2, SLOT_GIRL)] = (Unit){
        .alive=true, .team=TEAM_P2, .slot=SLOT_GIRL,
        .char_id=(b->p2_girl_id[0] ? b->p2_girl_id : "kiritan"),
        .pos=(Pos){18,12}, .stats=p2_girl
    };

    b->_has_cmd[TEAM_P1] = false;
    b->_has_cmd[TEAM_P2] = false;
    return true;
}

void battle_core_submit_cmd(BattleCore *b, Team team, const TurnCmd *cmd) {
    if (!b || !cmd) return;
    b->_pending_cmd[(int)team] = *cmd;
    b->_has_cmd[(int)team] = true;
}

void battle_core_build_action_order(const BattleCore *b, int out_idx[4], int *out_n) {
    int tmp[4], n = 0;
    for (int i = 0; i < 4; i++) if (b->units[i].alive) tmp[n++] = i;

    // SPD降順、同値は index 昇順
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            int a = tmp[i], c = tmp[j];
            int sa = b->units[a].stats.spd;
            int sc = b->units[c].stats.spd;
            if (sc > sa || (sc == sa && c < a)) {
                int t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t;
            }
        }
    }

    for (int i = 0; i < n; i++) out_idx[i] = tmp[i];
    *out_n = n;
}

// --- 新：段階実行 ---
bool battle_core_begin_exec(BattleCore *b) {
    if (!b) return false;
    if (b->phase == BPHASE_END) return false;
    if (b->_exec_active) return false;
    if (!b->_has_cmd[TEAM_P1] || !b->_has_cmd[TEAM_P2]) return false;

    b->phase = BPHASE_RESOLVE;
    b->last_executed_skill_id = NULL;
    b->_exec_active = true;
    return true;
}

void battle_core_exec_act_for_unit(BattleCore *b, int ui) {
    if (!b) return;
    if (!b->_exec_active) return;
    if (b->phase != BPHASE_RESOLVE) return;
    if (ui < 0 || ui >= 4) return;

    Unit *u = &b->units[ui];
    if (!u->alive) return;

    // 座標を盤面にクランプ（scene側保険）
    u->pos.x = (int8_t)clampi((int)u->pos.x, MAP_MIN, MAP_MAX);
    u->pos.y = (int8_t)clampi((int)u->pos.y, MAP_MIN, MAP_MAX);

    const UnitCmd *uc = &b->_pending_cmd[(int)u->team].cmd[(int)u->slot];
    do_skill(b, u->team, u->slot, uc);

    if (team_all_dead(b, TEAM_P1) || team_all_dead(b, TEAM_P2)) {
        b->phase = BPHASE_END;
    }
}

void battle_core_end_exec(BattleCore *b) {
    if (!b) return;
    if (!b->_exec_active) return;

    b->_exec_active = false;

    b->_has_cmd[TEAM_P1] = false;
    b->_has_cmd[TEAM_P2] = false;

    if (team_all_dead(b, TEAM_P1) || team_all_dead(b, TEAM_P2)) {
        b->phase = BPHASE_END;
        return;
    }

    apply_st_regen_end_of_turn(b);

    b->phase = BPHASE_INPUT;
    b->turn += 1;
}

// --- 旧：一括step（互換のため残す）---
bool battle_core_step(BattleCore *b) {
    if (!b) return false;
    if (b->phase == BPHASE_END) return false;

    if (!b->_has_cmd[TEAM_P1] || !b->_has_cmd[TEAM_P2]) return false;

    b->phase = BPHASE_RESOLVE;
    b->last_executed_skill_id = NULL;

    // 1) move
    apply_move_if_any(b, TEAM_P1, SLOT_HERO, &b->_pending_cmd[TEAM_P1].cmd[SLOT_HERO]);
    apply_move_if_any(b, TEAM_P1, SLOT_GIRL, &b->_pending_cmd[TEAM_P1].cmd[SLOT_GIRL]);
    apply_move_if_any(b, TEAM_P2, SLOT_HERO, &b->_pending_cmd[TEAM_P2].cmd[SLOT_HERO]);
    apply_move_if_any(b, TEAM_P2, SLOT_GIRL, &b->_pending_cmd[TEAM_P2].cmd[SLOT_GIRL]);

    // 2) actions
    int order[4], n = 0;
    battle_core_build_action_order(b, order, &n);

    for (int k = 0; k < n; k++) {
        int uidx = order[k];
        Unit *u = &b->units[uidx];
        if (!u->alive) continue;

        const UnitCmd *uc = &b->_pending_cmd[(int)u->team].cmd[(int)u->slot];
        do_skill(b, u->team, u->slot, uc);

        if (team_all_dead(b, TEAM_P1) || team_all_dead(b, TEAM_P2)) break;
    }

    // 3) end turn
    b->_has_cmd[TEAM_P1] = false;
    b->_has_cmd[TEAM_P2] = false;

    if (team_all_dead(b, TEAM_P1) || team_all_dead(b, TEAM_P2)) {
        b->phase = BPHASE_END;
    } else {
        apply_st_regen_end_of_turn(b);
        b->phase = BPHASE_INPUT;
        b->turn += 1;
    }
    return true;
}