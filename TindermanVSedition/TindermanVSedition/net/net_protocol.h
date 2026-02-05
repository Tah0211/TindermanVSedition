// net/net_protocol.h — オンライン対戦プロトコル定義
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "../battle/battle_cmd.h"

// メッセージ型 (1byte header)
#define MSG_READY         0x01
#define MSG_ASSIGN        0x02  // server -> client  payload: 1byte player_id
#define MSG_GAME_INFO     0x03  // client -> server  payload: 50bytes
#define MSG_OPPONENT_INFO 0x04  // server -> client  payload: 50bytes
#define MSG_TURN_CMD      0x05  // client -> server  payload: 14bytes (TurnCmd)
#define MSG_OPPONENT_CMD  0x06  // server -> client  payload: 14bytes (TurnCmd)

// GAME_INFO payload: girl_id[32] + stats i16×8(16) + tag(u8) + move_range(u8) = 50bytes
#define NET_GAME_INFO_BYTES 50

// メッセージ全体サイズ (header 1byte + payload)
#define MSG_READY_SIZE          1
#define MSG_ASSIGN_SIZE         2
#define MSG_GAME_INFO_SIZE     51
#define MSG_OPPONENT_INFO_SIZE 51
#define MSG_TURN_CMD_SIZE      15
#define MSG_OPPONENT_CMD_SIZE  15

// メッセージの最大サイズ
#define NET_MSG_MAX_SIZE       51

typedef struct {
    char    girl_id[32];
    int16_t hp_base;
    int16_t atk_base;
    int16_t sp_base;
    int16_t st_base;
    int16_t hp_add;
    int16_t atk_add;
    int16_t sp_add;
    int16_t st_add;
    uint8_t tag_learned;
    uint8_t move_range;
} NetGameInfo;

static inline void net_game_info_pack(const NetGameInfo *info, uint8_t out[NET_GAME_INFO_BYTES])
{
    memset(out, 0, NET_GAME_INFO_BYTES);
    memcpy(out, info->girl_id, 32);
    // LAN内同一アーキテクチャ前提でネイティブバイトオーダー
    memcpy(out + 32, &info->hp_base,  2);
    memcpy(out + 34, &info->atk_base, 2);
    memcpy(out + 36, &info->sp_base,  2);
    memcpy(out + 38, &info->st_base,  2);
    memcpy(out + 40, &info->hp_add,   2);
    memcpy(out + 42, &info->atk_add,  2);
    memcpy(out + 44, &info->sp_add,   2);
    memcpy(out + 46, &info->st_add,   2);
    out[48] = info->tag_learned;
    out[49] = info->move_range;
}

static inline void net_game_info_unpack(const uint8_t in[NET_GAME_INFO_BYTES], NetGameInfo *info)
{
    memset(info, 0, sizeof(*info));
    memcpy(info->girl_id, in, 32);
    info->girl_id[31] = '\0';
    memcpy(&info->hp_base,  in + 32, 2);
    memcpy(&info->atk_base, in + 34, 2);
    memcpy(&info->sp_base,  in + 36, 2);
    memcpy(&info->st_base,  in + 38, 2);
    memcpy(&info->hp_add,   in + 40, 2);
    memcpy(&info->atk_add,  in + 42, 2);
    memcpy(&info->sp_add,   in + 44, 2);
    memcpy(&info->st_add,   in + 46, 2);
    info->tag_learned = in[48];
    info->move_range  = in[49];
}

// msg_type からペイロードサイズを返す (-1: 不明)
static inline int net_msg_payload_size(uint8_t msg_type)
{
    switch (msg_type) {
    case MSG_READY:         return 0;
    case MSG_ASSIGN:        return 1;
    case MSG_GAME_INFO:     return NET_GAME_INFO_BYTES;
    case MSG_OPPONENT_INFO: return NET_GAME_INFO_BYTES;
    case MSG_TURN_CMD:      return TURNCMD_WIRE_BYTES;
    case MSG_OPPONENT_CMD:  return TURNCMD_WIRE_BYTES;
    default:                return -1;
    }
}
