// battle/battle_cmd.h
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "battle_types.h"

// skill_index:
//  - hero: 0..2
//  - girl: 0..(2 or 3)  ※tag_learnedなら3を許可
//  - -1: 技なし（待機）
typedef struct {
    bool has_move;
    Pos  move_to;         // has_move==falseなら無視
    int8_t skill_index;   // -1..3
    int8_t target;        // 単体技のみ：敵のslot指定 (0=hero,1=girl) / 範囲技は -1
} UnitCmd;

typedef struct {
    UnitCmd cmd[2]; // [SLOT_HERO], [SLOT_GIRL]
} TurnCmd;

// serializeは「将来のSDL_net/UDP/TCP」でも使えるように固定長にする
// 1TurnCmd = 2UnitCmd * (1+2+1+1) = 10 bytes程度で収まる
#define TURNCMD_WIRE_BYTES 10

bool battle_cmd_pack(const TurnCmd* in, uint8_t out[TURNCMD_WIRE_BYTES]);
bool battle_cmd_unpack(const uint8_t in[TURNCMD_WIRE_BYTES], TurnCmd* out);

// 自己検証用：値域チェック（オンラインでは必須）
bool battle_cmd_validate(const TurnCmd* c);
