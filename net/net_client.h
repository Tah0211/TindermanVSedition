// net_client.h — オンライン対戦クライアントAPI
#ifndef NET_CLIENT_H
#define NET_CLIENT_H

#include <stdbool.h>
#include "net_protocol.h"

// コマンドライン引数で設定される接続先（デフォルト: 127.0.0.1:12345）
extern const char *g_net_host;
extern int g_net_port;

void net_connect(const char *host, int port);
void net_send_ready(void);
void net_poll(void);
void net_disconnect(void);

// 接続状態
bool net_is_online(void);

// ASSIGN で割り当てられたplayer_id (0 or 1, 未割当=-1)
int  net_get_player_id(void);

// GAME_INFO送信
void net_send_game_info(const NetGameInfo *info);

// OPPONENT_INFO受信（受信済みならtrueを返しoutに書き込み、内部フラグクリア）
bool net_received_opponent_info(NetGameInfo *out);

// TURN_CMD送信
void net_send_turn_cmd(const TurnCmd *cmd);

// OPPONENT_CMD受信（受信済みならtrueを返しoutに書き込み、内部フラグクリア）
bool net_received_opponent_cmd(TurnCmd *out);

// 旧互換
int  net_received_start(void);

#endif
