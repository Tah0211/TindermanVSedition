// battle/char_defs.h
#pragma once
#include <stdbool.h>

typedef struct {
    const char* char_id;        // "hero" / "himari" / "kiritan"
    int st_regen_per_turn;      // ターン終了時などの回復量

    // 通常技（タッグ未習得でも使える）
    const char* skill_ids[4];   // 最大3想定だが余裕で4
    int skill_count;

    // タッグ技（未習得なら無効）
    const char* tag_skill_id;   // 例: "himari_tag" / NULL
} CharDef;

// char_id から定義を取得（無ければNULL）
const CharDef* char_def_get(const char* char_id);

// このキャラが「今」選べる技数（tag_learned込み）
int char_def_get_available_skill_count(const CharDef* cd, bool tag_learned);

// index -> skill_id（tag_learned込み）。範囲外はNULL
const char* char_def_get_skill_id_at(const CharDef* cd, bool tag_learned, int index);
