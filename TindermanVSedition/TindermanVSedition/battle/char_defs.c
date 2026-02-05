// battle/char_defs.c
#include "char_defs.h"
#include <string.h>

static const CharDef g_chars[] = {
    // 主人公（例：タッグ技なし）
    {
        .char_id = "hero",
        .st_regen_per_turn = 5,
        .skill_ids = { "hero_tech1", "hero_tech2", "hero_tech3", NULL },
        .skill_count = 3,
        .tag_skill_id = NULL
    },

    // ひまり
    {
        .char_id = "himari",
        .st_regen_per_turn = 3,
        .skill_ids = { "himari_1", "himari_2", "himari_3", NULL },
        .skill_count = 3,
        .tag_skill_id = "himari_tag"
    },

    // きりたん
    {
        .char_id = "kiritan",
        .st_regen_per_turn = 10,
        .skill_ids = { "kiritan_1", "kiritan_2", "kiritan_3", NULL },
        .skill_count = 3,
        .tag_skill_id = "kiritan_tag"
    },
};

const CharDef* char_def_get(const char* char_id){
    if(!char_id) return NULL;
    for(size_t i=0;i<sizeof(g_chars)/sizeof(g_chars[0]);i++){
        if(strcmp(g_chars[i].char_id, char_id)==0) return &g_chars[i];
    }
    return NULL;
}

int char_def_get_available_skill_count(const CharDef* cd, bool tag_learned){
    if(!cd) return 0;
    int n = cd->skill_count;
    if(tag_learned && cd->tag_skill_id) n += 1;
    return n;
}

const char* char_def_get_skill_id_at(const CharDef* cd, bool tag_learned, int index){
    if(!cd) return NULL;
    if(index < 0) return NULL;

    if(index < cd->skill_count){
        return cd->skill_ids[index];
    }

    // タッグ枠は最後
    if(tag_learned && cd->tag_skill_id && index == cd->skill_count){
        return cd->tag_skill_id;
    }

    return NULL;
}
