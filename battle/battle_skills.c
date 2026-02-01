// battle/battle_skills.c
#include "battle_skills.h"
#include <string.h>
#include <stdio.h>

static const SkillDef g_skills[] = {
    // =========================
    // hero
    // =========================
    // 技1：ST15 / 射程3 / ATK
    { "hero_tech1", "アルティメットインパクト", SKTYPE_ATTACK, 3, 15, SKT_SINGLE, 0, 0, 1, false },

    // 技2：ST30 / 射程8 / ATK+10
    { "hero_tech2", "魔閃光", SKTYPE_ATTACK, 8, 30, SKT_SINGLE, 10, 0, 1, false },

    // 技3：ST50 / 射程∞ / 味方全員HP+30
    // range=-1 を「射程∞」として扱う（射程判定/表示はスキップ）
    { "hero_tech3", "全体回復", SKTYPE_HEAL, -1, 50, SKT_AOE, 30, 0, 1, false },

    // =========================
    // himari
    // =========================
    // 技1：ST5 / 射程2 / ATK
    { "himari_1", "俊閃連撃", SKTYPE_ATTACK, 2, 5, SKT_SINGLE, 0, 0, 1, false },

    // 技2：カウンター ST30 / 自身から4マス以内の敵から攻撃を受けたとき / 敵ATK×2
    // 表現：COUNTER / range=4 / mult=2 / power=0
    { "himari_2", "メトロアタック", SKTYPE_COUNTER, 4, 30, SKT_SINGLE, 0, 0, 2, false },

    // 技3：ST30 / 射程5 / ATK+10
    { "himari_3", "神速演舞", SKTYPE_ATTACK, 5, 30, SKT_SINGLE, 10, 0, 1, false },

    // タッグ：バトル中1回 / 射程5 / ATK×3
    { "himari_tag", "ひまりTAG", SKTYPE_ATTACK, 5, 30, SKT_SINGLE, 0, 0, 3, true },

    // =========================
    // kiritan
    // =========================
    // 技1：ST20 / 射程12 / ATK+10
    { "kiritan_1", "裁きの刃", SKTYPE_ATTACK, 12, 20, SKT_SINGLE, 10, 0, 1, false },

    // 技2：ST35 / 射程16 / ATK+10
    { "kiritan_2", "何とかビーム", SKTYPE_ATTACK, 16, 35, SKT_SINGLE, 10, 0, 1, false },

    // 技3：ST40 / 射程12（範囲攻撃）/ ATK+15 / 半径1
    { "kiritan_3", "スターダストフォール", SKTYPE_ATTACK, -1, 40, SKT_AOE, 15, 8, 1, false },

    // タッグ：バトル中1回 / 射程10 / ATK×6
    { "kiritan_tag", "覚醒の一撃", SKTYPE_ATTACK, 10, 40, SKT_SINGLE, 0, 0, 6, true },

    // =========================
    // fallback（未知のgirl用）
    // =========================
    { "girl_1", "ガール1", SKTYPE_ATTACK, 3, 10, SKT_SINGLE, 0, 0, 1, false },
    { "girl_2", "ガール2", SKTYPE_ATTACK, 4, 20, SKT_SINGLE, 10, 0, 1, false },
    { "girl_3", "ガール範囲", SKTYPE_ATTACK, 4, 30, SKT_AOE, 10, 1, 1, false },
    { "girl_tag", "ガールTAG", SKTYPE_ATTACK, 4, 40, SKT_SINGLE, 0, 0, 3, true },
};

const SkillDef* battle_skill_get(const char* skill_id)
{
    if (!skill_id) return NULL;

    const size_t n = sizeof(g_skills) / sizeof(g_skills[0]);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(g_skills[i].id, skill_id) == 0) {
            return &g_skills[i];
        }
    }
    return NULL;
}

const char* battle_skill_movie_path(const char* skill_id)
{
    static char path[256];
    if (!skill_id) return NULL;

    // ここは「skill_idのファイル名を信用する」前提（必要ならバリデーション追加）
    snprintf(path, sizeof(path), "assets/cutin/%s.mp4", skill_id);
    return path;
}
