// battle/battle_skills.c
#include "battle_skills.h"
#include <string.h>
#include <stdio.h>

static const SkillDef g_skills[] = {
    // --- hero ---
    { "hero_tech1",  SKTYPE_ATTACK, 3,  5,  SKT_SINGLE,  2, 0 },
    { "hero_tech2",  SKTYPE_ATTACK, 2,  8,  SKT_SINGLE,  4, 0 },
    { "hero_tech3",  SKTYPE_ATTACK, 4, 10,  SKT_AOE,     1, 1 }, // aoe_radius=1

    // --- himari ---
    { "himari_1",    SKTYPE_ATTACK, 2,  6,  SKT_SINGLE,  3, 0 },
    { "himari_2",    SKTYPE_ATTACK, 3,  8,  SKT_SINGLE,  2, 0 },
    { "himari_3",    SKTYPE_ATTACK, 3, 10,  SKT_AOE,     1, 1 },
    { "himari_tag",  SKTYPE_ATTACK, 3, 20,  SKT_AOE,     6, 1 },

    // --- kiritan ---
    { "kiritan_1",   SKTYPE_ATTACK, 4,  6,  SKT_SINGLE,  2, 0 },
    { "kiritan_2",   SKTYPE_ATTACK, 5,  8,  SKT_SINGLE,  3, 0 },
    { "kiritan_3",   SKTYPE_ATTACK, 4, 10,  SKT_AOE,     1, 1 },
    { "kiritan_tag", SKTYPE_ATTACK, 4, 20,  SKT_AOE,     6, 1 },

    // --- fallback ---
    { "girl_1",      SKTYPE_ATTACK, 3,  6,  SKT_SINGLE,  2, 0 },
    { "girl_2",      SKTYPE_ATTACK, 3,  8,  SKT_SINGLE,  3, 0 },
    { "girl_3",      SKTYPE_ATTACK, 3, 10,  SKT_AOE,     1, 1 },
    { "girl_tag",    SKTYPE_ATTACK, 3, 20,  SKT_AOE,     6, 1 },

    // ★ここから先に、回復・カウンター技を足していける
    // 例：
    // { "hero_counter", SKTYPE_COUNTER, 3, 12, SKT_SINGLE, 2, 0 },
    // { "himari_heal",  SKTYPE_HEAL,    0, 10, SKT_SINGLE, 8, 0 },
};

const SkillDef* battle_skill_get(const char* skill_id){
    if(!skill_id) return NULL;
    for(size_t i=0;i<sizeof(g_skills)/sizeof(g_skills[0]);i++){
        if(strcmp(g_skills[i].id, skill_id)==0) return &g_skills[i];
    }
    return NULL;
}

const char* battle_skill_movie_path(const char* skill_id){
    static char path[256];
    if(!skill_id) return NULL;
    snprintf(path, sizeof(path), "assets/cutin/%s.mp4", skill_id);
    return path;
}
