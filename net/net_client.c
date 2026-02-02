#include "net_client.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/select.h>

const char *g_net_host = "127.0.0.1";
int g_net_port = 12345;

static int sock = -1;

// 受信バッファ（TCPストリーム分割対応）
#define RECV_BUF_SIZE 512
static uint8_t recv_buf[RECV_BUF_SIZE];
static int recv_len = 0;

// 状態
static int  player_id = -1;  // ASSIGN で割り当て
static bool has_opponent_info = false;
static NetGameInfo opponent_info;
static bool has_opponent_cmd = false;
static TurnCmd opponent_cmd;

// 確実に全バイト書き込む
static int send_all(const uint8_t *data, int len)
{
    if (sock < 0) return -1;
    int sent = 0;
    while (sent < len) {
        ssize_t n = write(sock, data + sent, len - sent);
        if (n <= 0) {
            perror("[net] write");
            return -1;
        }
        sent += (int)n;
    }
    return 0;
}

void net_connect(const char *host, int port)
{
    struct hostent *server;
    struct sockaddr_in addr;

    printf("[net] net_connect(%s, %d)\n", host, port);

    server = gethostbyname(host);
    if (!server) {
        fprintf(stderr, "[net] gethostbyname failed\n");
        return;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("[net] socket");
        return;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[net] connect");
        close(sock);
        sock = -1;
        return;
    }

    // 状態リセット
    recv_len = 0;
    player_id = -1;
    has_opponent_info = false;
    has_opponent_cmd = false;

    printf("[net] connected to server\n");
}

void net_disconnect(void)
{
    if (sock >= 0) {
        close(sock);
        sock = -1;
    }
    recv_len = 0;
    player_id = -1;
    has_opponent_info = false;
    has_opponent_cmd = false;
    printf("[net] disconnected\n");
}

bool net_is_online(void)
{
    return sock >= 0;
}

int net_get_player_id(void)
{
    return player_id;
}

void net_send_ready(void)
{
    uint8_t msg[1] = { MSG_READY };
    if (send_all(msg, 1) == 0) {
        printf("[net] SEND READY\n");
    }
}

void net_send_game_info(const NetGameInfo *info)
{
    uint8_t msg[1 + NET_GAME_INFO_BYTES];
    msg[0] = MSG_GAME_INFO;
    net_game_info_pack(info, msg + 1);
    if (send_all(msg, sizeof(msg)) == 0) {
        printf("[net] SEND GAME_INFO\n");
    }
}

void net_send_turn_cmd(const TurnCmd *cmd)
{
    uint8_t msg[1 + TURNCMD_WIRE_BYTES];
    msg[0] = MSG_TURN_CMD;
    if (!battle_cmd_pack(cmd, msg + 1)) {
        fprintf(stderr, "[net] battle_cmd_pack failed\n");
        return;
    }
    if (send_all(msg, sizeof(msg)) == 0) {
        printf("[net] SEND TURN_CMD\n");
    }
}

bool net_received_opponent_info(NetGameInfo *out)
{
    if (!has_opponent_info) return false;
    if (out) *out = opponent_info;
    has_opponent_info = false;
    return true;
}

bool net_received_opponent_cmd(TurnCmd *out)
{
    if (!has_opponent_cmd) return false;
    if (out) *out = opponent_cmd;
    has_opponent_cmd = false;
    return true;
}

int net_received_start(void)
{
    // 旧互換: ASSIGN受信済み = マッチング成立
    return (player_id >= 0) ? 1 : 0;
}

// 1メッセージを処理
static void handle_message(uint8_t msg_type, const uint8_t *payload, int payload_len)
{
    (void)payload_len;

    switch (msg_type) {
    case MSG_ASSIGN:
        player_id = (int)payload[0];
        printf("[net] RECV ASSIGN: player_id=%d\n", player_id);
        break;

    case MSG_OPPONENT_INFO:
        net_game_info_unpack(payload, &opponent_info);
        has_opponent_info = true;
        printf("[net] RECV OPPONENT_INFO: girl_id=%s\n", opponent_info.girl_id);
        break;

    case MSG_OPPONENT_CMD:
        if (!battle_cmd_unpack(payload, &opponent_cmd)) {
            fprintf(stderr, "[net] battle_cmd_unpack failed\n");
            break;
        }
        has_opponent_cmd = true;
        printf("[net] RECV OPPONENT_CMD\n");
        break;

    default:
        printf("[net] Unknown msg_type 0x%02x\n", msg_type);
        break;
    }
}

// 受信バッファからメッセージを切り出して処理
static void process_recv_buf(void)
{
    while (recv_len > 0) {
        uint8_t msg_type = recv_buf[0];
        int psize = net_msg_payload_size(msg_type);
        if (psize < 0) {
            fprintf(stderr, "[net] Invalid msg_type 0x%02x, closing\n", msg_type);
            net_disconnect();
            return;
        }

        int total = 1 + psize;
        if (recv_len < total) break;

        handle_message(msg_type, recv_buf + 1, psize);

        int remain = recv_len - total;
        if (remain > 0) {
            memmove(recv_buf, recv_buf + total, remain);
        }
        recv_len = remain;
    }
}

void net_poll(void)
{
    if (sock < 0) return;

    fd_set rfds;
    struct timeval tv = { 0, 0 };

    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);

    int ret = select(sock + 1, &rfds, NULL, NULL, &tv);
    if (ret <= 0) return;

    if (!FD_ISSET(sock, &rfds)) return;

    int space = RECV_BUF_SIZE - recv_len;
    if (space <= 0) {
        fprintf(stderr, "[net] recv buffer full\n");
        net_disconnect();
        return;
    }

    ssize_t n = read(sock, recv_buf + recv_len, space);
    if (n <= 0) {
        if (n == 0) printf("[net] server closed connection\n");
        else perror("[net] read");
        net_disconnect();
        return;
    }

    recv_len += (int)n;
    process_recv_buf();
}
