// battle/battle_skills.h
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SKTYPE_ATTACK = 0,
    SKTYPE_HEAL   = 1,
    SKTYPE_COUNTER= 2,
} SkillType;

typedef enum {
    SKT_SINGLE = 0,   // 単体
    SKT_AOE    = 1,   // 範囲/全体
} SkillTarget;

typedef struct {
    const char *id;          // 内部ID（cmd/jsonの参照キー）
    const char *name;        // 表示名（UI/ログ用）
    SkillType type;          // ATTACK / HEAL / COUNTER

    int range;               // 射程（>=0: manhattan距離, <0: 無限扱い）
    int st_cost;             // ST消費

    SkillTarget target;      // SINGLE / AOE
    int power;               // ATTACK: ATK加算, HEAL: 固定回復量, COUNTER: ATK加算(通常0)
    int aoe_radius;          // AOE半径（SINGLEのとき0推奨）
    int multiplier;          // ATTACK/COUNTER: 倍率（damageに掛ける）
    bool once_per_battle;    // タッグ等（1戦1回）
} SkillDef;

// skill_id から定義を引く（見つからなければNULL）
const SkillDef* battle_skill_get(const char* skill_id);

// cutin動画パスを生成（assets/cutin/<skill_id>.mp4）
const char* battle_skill_movie_path(const char* skill_id);

// ===== 便利関数（ロジック側での一貫解釈）=====

// 射程判定を行うべきか（range<0 は射程無限=判定スキップ）
static inline bool battle_skill_should_check_range(const SkillDef* s) {
    return s && s->range >= 0;
}

// ダメージ式（あなたの合意：damage = (ATK + power) * multiplier）
static inline int battle_skill_calc_attack_damage(const SkillDef* s, int atk) {
    if (!s) return 0;
    int base = atk + s->power;
    if (base < 0) base = 0;
    int mult = (s->multiplier <= 0) ? 1 : s->multiplier;
    return base * mult;
}

// 回復式（あなたの合意：power固定）
static inline int battle_skill_calc_heal_amount(const SkillDef* s) {
    if (!s) return 0;
    return (s->power < 0) ? 0 : s->power;
}

// カウンター式（基本は攻撃式を流用。敵ATKを入れる想定）
static inline int battle_skill_calc_counter_damage(const SkillDef* s, int enemy_atk) {
    return battle_skill_calc_attack_damage(s, enemy_atk);
}

#ifdef __cplusplus
}
#endif
