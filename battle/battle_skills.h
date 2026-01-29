// battle/battle_skills.h
#pragma once
#include <stdbool.h>
#include "battle_types.h"

typedef enum { SKT_SINGLE=0, SKT_AOE=1 } SkillTargetType;

// ★追加：技タイプ
typedef enum {
    SKTYPE_ATTACK = 0,   // 攻撃（単体/範囲は tgt で）
    SKTYPE_COUNTER,      // カウンター構え（状態付与）
    SKTYPE_HEAL          // 回復
} SkillType;

typedef struct {
    const char* id;       // "hero_tech1" 等
    SkillType type;       // ★追加
    int range;            // マンハッタン射程（attack/counter）
    int st_cost;          // 成立時のみ消費（counterは「構え時に消費」推奨）
    SkillTargetType tgt;  // SKTYPE_ATTACK のときに意味あり
    int power;            // 攻撃：atk+power / 回復：回復量として流用
    int aoe_radius;       // ★追加：AOE半径（0なら1扱い）
} SkillDef;

const SkillDef* battle_skill_get(const char* skill_id);
const char* battle_skill_movie_path(const char* skill_id);
