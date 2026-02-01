// battle/battle_core.c
#include "battle_core.h"
#include <string.h>
#include <stdio.h>

#include "battle_skills.h"  // battle_skill_get()
#include "char_defs.h"      // char_def_get(), char_def_get_skill_id_at()

#define MAP_MIN 0
#define MAP_MAX 20

// ---------------------------------
// util
// ---------------------------------
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
    if (range < 0) return true; // range=-1 は射程∞
    return manhattan(a, b) <= range;
}

static bool in_range_manhattan_units(const Unit *a, const Unit *t, int range) {
    if (!a || !t) return false;
    return in_range_manhattan_pos(a->pos, t->pos, range);
}

// ---------------------------------
// event queue
// ---------------------------------
void battle_core_clear_events(BattleCore *b) {
    if (!b) return;
    b->ev_count = 0;
}

int battle_core_event_count(const BattleCore *b) {
    if (!b) return 0;
    return b->ev_count;
}

const BattleEvent* battle_core_get_event(const BattleCore *b, int idx) {
    if (!b) return NULL;
    if (idx < 0 || idx >= b->ev_count) return NULL;
    return &b->events[idx];
}

static void push_event(BattleCore *b, BattleEvent ev) {
    if (!b) return;
    if (b->ev_count >= BATTLE_EVENT_MAX) return;
    b->events[b->ev_count++] = ev;
}

static void push_anim(BattleCore *b, int actor_ui, int target_ui, const char *skill_id, Pos center, int radius) {
    BattleEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = BEV_ANIM_SKILL;
    ev.actor_ui = actor_ui;
    ev.target_ui = target_ui;
    ev.center = center;
    ev.radius = radius;
    ev.value = 0;
    ev.skill_id = skill_id;
    push_event(b, ev);

    // 互換：scene側が「最後に成立した技」を参照している場合に備えて入れておく
    b->last_executed_skill_id = skill_id;
    b->last_executed_actor_ui = actor_ui;
    b->last_executed_target_ui = target_ui;
}

static void push_damage(BattleCore *b, int actor_ui, int target_ui, int dmg) {
    if (dmg < 1) dmg = 1;
    BattleEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = BEV_EFFECT_DAMAGE;
    ev.actor_ui = actor_ui;
    ev.target_ui = target_ui;
    ev.value = dmg;
    ev.skill_id = NULL;
    push_event(b, ev);
}

static void push_heal(BattleCore *b, int actor_ui, int target_ui, int heal) {
    if (heal < 1) heal = 1;
    BattleEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = BEV_EFFECT_HEAL;
    ev.actor_ui = actor_ui;
    ev.target_ui = target_ui;
    ev.value = heal;
    ev.skill_id = NULL;
    push_event(b, ev);
}

// ---------------------------------
// effect application (delayed)
// ---------------------------------
static void apply_damage_raw(Unit *tgt, int dmg) {
    if (!tgt || !tgt->alive) return;
    if (dmg < 1) dmg = 1;

    tgt->stats.hp -= dmg;
    if (tgt->stats.hp <= 0) {
        tgt->stats.hp = 0;
        tgt->alive = false;
    }
}

static void apply_heal_raw(BattleCore *b, int tidx, int amount) {
    if (!b) return;
    if (tidx < 0 || tidx >= 4) return;

    Unit *tgt = &b->units[tidx];
    if (!tgt->alive) return;
    if (amount < 1) amount = 1;

    int maxhp = b->hp_max[tidx];
    if (maxhp < 1) {
        // 念のため：未初期化なら現HPを最大として扱う
        maxhp = tgt->stats.hp;
        if (maxhp < 1) maxhp = 1;
        b->hp_max[tidx] = maxhp;
    }

    int nhp = tgt->stats.hp + amount;
    if (nhp > maxhp) nhp = maxhp;
    tgt->stats.hp = nhp;
}

void battle_core_apply_event(BattleCore *b, const BattleEvent *ev) {
    if (!b || !ev) return;

    switch (ev->type) {
    case BEV_EFFECT_DAMAGE:
        if (ev->target_ui >= 0 && ev->target_ui < 4) {
            apply_damage_raw(&b->units[ev->target_ui], ev->value);
        }
        break;
    case BEV_EFFECT_HEAL:
        apply_heal_raw(b, ev->target_ui, ev->value);
        break;
    case BEV_ANIM_SKILL:
    default:
        // 演出はscene側が扱う
        break;
    }
}

void battle_core_apply_events(BattleCore *b) {
    if (!b) return;

    for (int i = 0; i < b->ev_count; ++i) {
        battle_core_apply_event(b, &b->events[i]);
    }

    // 適用後に勝敗判定
    if (team_all_dead(b, TEAM_P1) || team_all_dead(b, TEAM_P2)) {
        b->phase = BPHASE_END;
    }

    battle_core_clear_events(b);
}

// ---------------------------------
// misc
// ---------------------------------
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

static const char* resolve_skill_id_for_actor(const BattleCore *b, const Unit *actor, int skill_index) {
    if (!b || !actor) return NULL;
    if (skill_index < 0) return NULL;

    const CharDef *cd = char_def_get(actor->char_id);
    if (!cd) return NULL;

    bool tag = is_tag_learned_for_team(b, actor->team);
    return char_def_get_skill_id_at(cd, tag, skill_index);
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

// ---------------------------------
// skill resolve (eventized)
// ---------------------------------
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

    // -------------------------
    // COUNTER：対象不要（自分に状態付与）
    //   - 構えは「使った時点でST消費」
    //   - 構え成立時にのみ状態付与（演出はここでは出さない）
    // -------------------------
    if (sk->type == SKTYPE_COUNTER) {
        if (!spend_st_if_possible(att, sk->st_cost)) return;

        b->counter_ready[aidx] = true;
        b->counter_range[aidx] = sk->range;
        b->counter_skill_id[aidx] = skill_id;
        return;
    }

    // target slot は 0=hero, 1=girl を共通利用
    Slot ts = (uc->target == 1) ? SLOT_GIRL : SLOT_HERO;

    // -------------------------
    // HEAL：味方を対象
    //   - STは「使った時点で」消費
    //   - 単体は射程判定（マンハッタン）
    //   - 成立した瞬間に ANIM を出す（回復適用は後）
    // -------------------------
    if (sk->type == SKTYPE_HEAL) {
        int heal = sk->power;
        if (heal < 1) heal = 1;

        if (!spend_st_if_possible(att, sk->st_cost)) return;

        if (sk->target == SKT_SINGLE) {
            int tidx = unit_index(actor_team, ts);
            Unit *tgt = &b->units[tidx];
            if (!tgt->alive) return;

            if (!in_range_manhattan_units(att, tgt, sk->range)) return;

            // 成立
            push_anim(b, aidx, tidx, skill_id, (Pos){0,0}, 0);
            push_heal(b, aidx, tidx, heal);
        } else {
            // 範囲回復：味方全体（hero + girl）
            int iH = unit_index(actor_team, SLOT_HERO);
            int iG = unit_index(actor_team, SLOT_GIRL);

            // 成立（演出は1回）
            push_anim(b, aidx, -1, skill_id, (Pos){0,0}, 0);

            if (b->units[iH].alive) push_heal(b, aidx, iH, heal);
            if (b->units[iG].alive) push_heal(b, aidx, iG, heal);
        }
        return;
    }

    // -------------------------
    // ATTACK：敵を対象
    //   - 単体：射程判定（マンハッタン）
    //   - AOE：中心+半径（マンハッタン距離）
    //   - STは「使った時点で」消費（外しても消費）
    //   - 成立した瞬間に ANIM を出す（効果適用は後）
    //   - カウンターが立っている単体対象なら：
    //       敵攻撃を無効化（敵演出なし）→ カウンター演出 →（射程内なら）2倍反撃
    // -------------------------
    if (sk->type == SKTYPE_ATTACK) {
        Team enemy = (actor_team == TEAM_P1) ? TEAM_P2 : TEAM_P1;

        // まずST消費（使った時点）
        if (!spend_st_if_possible(att, sk->st_cost)) return;

        int dmg = calc_atk_plus_power(att, sk->power);

        if (sk->target == SKT_SINGLE) {
            int tidx = unit_index(enemy, ts);
            Unit *tgt = &b->units[tidx];
            if (!tgt->alive) return;

            // 射程外：不成立（STは消費済み）
            if (!in_range_manhattan_units(att, tgt, sk->range)) return;

            // ---- カウンター判定（対象が構え中なら、こちらの攻撃を無効化） ----
            if (b->counter_ready[tidx]) {
                // 構えは消費（発動してもしなくても解除）
                b->counter_ready[tidx] = false;

                const char *cid = b->counter_skill_id[tidx];
                int cr = b->counter_range[tidx];

                // カウンター発動：必ず演出（敵側の演出は出さない）
                push_anim(b, tidx, aidx, cid ? cid : "counter", (Pos){0,0}, 0);

                // 射程内なら反撃（ダメージ=「本来与えるはずだった dmg」の2倍）
                if (tgt->alive && att->alive && in_range_manhattan_pos(tgt->pos, att->pos, cr)) {
                    int cdmg = dmg * 2;
                    if (cdmg < 1) cdmg = 1;
                    push_damage(b, tidx, aidx, cdmg);
                }
                return;
            }

            // 通常成立：演出→ダメージ
            push_anim(b, aidx, tidx, skill_id, (Pos){0,0}, 0);
            push_damage(b, aidx, tidx, dmg);
            return;
        }

        // ---- AOE（射程なし） ----
        Pos c = uc->center;
        c.x = (int8_t)clampi((int)c.x, MAP_MIN, MAP_MAX);
        c.y = (int8_t)clampi((int)c.y, MAP_MIN, MAP_MAX);

        int r = (sk->aoe_radius <= 0) ? 1 : sk->aoe_radius;

        // 成立：演出1回
        push_anim(b, aidx, -1, skill_id, c, r);

        // 影響：中心からマンハッタン <= r の全員
        for (int i = 0; i < 4; i++) {
            Unit *u = &b->units[i];
            if (!u->alive) continue;
            if (manhattan(u->pos, c) > r) continue;

            if (u->team == enemy) {
                push_damage(b, aidx, i, dmg);
            } else {
                int half = dmg / 2; // 切り捨て
                if (half < 1) half = 1;
                push_damage(b, aidx, i, half);
            }
        }
        return;
    }

    // 未知 type は不発
}

// ---------------------------------
// public API
// ---------------------------------
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
    b->last_executed_actor_ui = -1;
    b->last_executed_target_ui = -1;
    b->_exec_active = false;
    b->ev_count = 0;

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

    // 最大HPは「初期HP＝最大」として保存
    for (int i = 0; i < 4; ++i) {
        int hp = b->units[i].stats.hp;
        if (hp < 1) hp = 1;
        b->hp_max[i] = hp;

        b->counter_ready[i] = false;
        b->counter_range[i] = 0;
        b->counter_skill_id[i] = NULL;
    }

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

// --- 段階実行 ---
bool battle_core_begin_exec(BattleCore *b) {
    if (!b) return false;
    if (b->phase == BPHASE_END) return false;
    if (b->_exec_active) return false;
    if (!b->_has_cmd[TEAM_P1] || !b->_has_cmd[TEAM_P2]) return false;

    b->phase = BPHASE_RESOLVE;
    b->last_executed_skill_id = NULL;
    b->last_executed_actor_ui = -1;
    b->last_executed_target_ui = -1;
    b->_exec_active = true;
    battle_core_clear_events(b);
    return true;
}

void battle_core_exec_act_for_unit(BattleCore *b, int ui) {
    if (!b) return;
    if (!b->_exec_active) return;
    if (b->phase != BPHASE_RESOLVE) return;
    if (ui < 0 || ui >= 4) return;

    Unit *u = &b->units[ui];
    if (!u->alive) {
        battle_core_clear_events(b);
        return;
    }

    // 直近イベントは「このユニットのアクションだけ」を保持
    battle_core_clear_events(b);

    // 座標を盤面にクランプ（scene側保険）
    u->pos.x = (int8_t)clampi((int)u->pos.x, MAP_MIN, MAP_MAX);
    u->pos.y = (int8_t)clampi((int)u->pos.y, MAP_MIN, MAP_MAX);

    const UnitCmd *uc = &b->_pending_cmd[(int)u->team].cmd[(int)u->slot];
    do_skill(b, u->team, u->slot, uc);

    // HP反映は scene 側が battle_core_apply_events() を呼ぶタイミングで行う
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
// ※ event化したので、旧stepは「即時適用」モードとして実装する
bool battle_core_step(BattleCore *b) {
    if (!b) return false;
    if (b->phase == BPHASE_END) return false;

    if (!b->_has_cmd[TEAM_P1] || !b->_has_cmd[TEAM_P2]) return false;

    b->phase = BPHASE_RESOLVE;
    b->last_executed_skill_id = NULL;
    b->last_executed_actor_ui = -1;
    b->last_executed_target_ui = -1;

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

        // exec + apply immediately
        battle_core_clear_events(b);
        do_skill(b, u->team, u->slot, uc);
        battle_core_apply_events(b);

        if (b->phase == BPHASE_END) break;
    }

    // 3) end turn
    b->_has_cmd[TEAM_P1] = false;
    b->_has_cmd[TEAM_P2] = false;

    if (b->phase == BPHASE_END) return true;

    apply_st_regen_end_of_turn(b);
    b->phase = BPHASE_INPUT;
    b->turn += 1;
    return true;
}
