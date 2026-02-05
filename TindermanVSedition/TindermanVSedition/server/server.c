// server.c — オンライン対戦リレーサーバ
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>

// プロトコル定数のみ使用（inline関数は不要なのでサイズ定義だけ再定義）
#define MSG_READY         0x01
#define MSG_ASSIGN        0x02
#define MSG_GAME_INFO     0x03
#define MSG_OPPONENT_INFO 0x04
#define MSG_TURN_CMD      0x05
#define MSG_OPPONENT_CMD  0x06

#define NET_GAME_INFO_BYTES 50
#define TURNCMD_WIRE_BYTES  14
#define NET_MSG_MAX_SIZE    51

#define MAX_CLIENTS 2
#define RECV_BUF_SIZE 256

// サーバ状態
typedef enum {
    STATE_WAITING,       // クライアント接続/READY待ち
    STATE_MATCHED,       // 両者READY → ASSIGN送信済
    STATE_INFO_EXCHANGE, // GAME_INFO交換中
    STATE_BATTLE         // 戦闘中（TURN_CMD中継）
} ServerState;

static int listen_sock;
static int client_sock[MAX_CLIENTS];
static int connected = 0;
static int ready[MAX_CLIENTS];
static ServerState state = STATE_WAITING;

// 受信バッファ（TCPストリーム分割対応）
static uint8_t recv_buf[MAX_CLIENTS][RECV_BUF_SIZE];
static int recv_len[MAX_CLIENTS];

// GAME_INFO受信済みフラグ
static uint8_t game_info[MAX_CLIENTS][NET_GAME_INFO_BYTES];
static int has_game_info[MAX_CLIENTS];

// TURN_CMD受信済みフラグ
static uint8_t turn_cmd[MAX_CLIENTS][TURNCMD_WIRE_BYTES];
static int has_turn_cmd[MAX_CLIENTS];

static int msg_payload_size(uint8_t msg_type)
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

// 確実に全バイト書き込む
static int send_all(int fd, const uint8_t *data, int len)
{
    int sent = 0;
    while (sent < len) {
        ssize_t n = write(fd, data + sent, len - sent);
        if (n <= 0) return -1;
        sent += (int)n;
    }
    return 0;
}

static void reset_session(void)
{
    state = STATE_WAITING;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        ready[i] = 0;
        recv_len[i] = 0;
        has_game_info[i] = 0;
        has_turn_cmd[i] = 0;
    }
    printf("[server] Session reset -> WAITING\n");
}

static void disconnect_client(int i, fd_set *mask)
{
    if (client_sock[i] >= 0) {
        printf("[server] Client %d disconnected\n", i);
        close(client_sock[i]);
        FD_CLR(client_sock[i], mask);
        client_sock[i] = -1;
        connected--;
    }
    reset_session();
}

// 1メッセージを処理
static void handle_message(int i, uint8_t msg_type, const uint8_t *payload, int payload_len, fd_set *mask)
{
    int other = 1 - i;
    (void)payload_len;

    switch (msg_type) {
    case MSG_READY:
        if (state != STATE_WAITING) break;
        ready[i] = 1;
        printf("[server] Client %d READY\n", i);

        if (connected == 2 && ready[0] && ready[1]) {
            // 両者READY → ASSIGN送信
            uint8_t assign0[2] = { MSG_ASSIGN, 0 };
            uint8_t assign1[2] = { MSG_ASSIGN, 1 };
            send_all(client_sock[0], assign0, 2);
            send_all(client_sock[1], assign1, 2);
            state = STATE_MATCHED;
            printf("[server] Both READY -> MATCHED, ASSIGN sent\n");

            // MATCHED直後にINFO_EXCHANGEへ
            state = STATE_INFO_EXCHANGE;
            has_game_info[0] = 0;
            has_game_info[1] = 0;
        }
        break;

    case MSG_GAME_INFO:
        if (state != STATE_INFO_EXCHANGE) break;
        if (payload_len != NET_GAME_INFO_BYTES) break;

        memcpy(game_info[i], payload, NET_GAME_INFO_BYTES);
        has_game_info[i] = 1;
        printf("[server] Client %d GAME_INFO received\n", i);

        if (has_game_info[0] && has_game_info[1]) {
            // 両者のGAME_INFOを相手にOPPONENT_INFOとして転送
            uint8_t msg[1 + NET_GAME_INFO_BYTES];

            msg[0] = MSG_OPPONENT_INFO;
            memcpy(msg + 1, game_info[1], NET_GAME_INFO_BYTES);
            send_all(client_sock[0], msg, sizeof(msg));

            msg[0] = MSG_OPPONENT_INFO;
            memcpy(msg + 1, game_info[0], NET_GAME_INFO_BYTES);
            send_all(client_sock[1], msg, sizeof(msg));

            state = STATE_BATTLE;
            has_turn_cmd[0] = 0;
            has_turn_cmd[1] = 0;
            printf("[server] INFO exchanged -> BATTLE\n");
        }
        break;

    case MSG_TURN_CMD:
        if (state != STATE_BATTLE) break;
        if (payload_len != TURNCMD_WIRE_BYTES) break;

        memcpy(turn_cmd[i], payload, TURNCMD_WIRE_BYTES);
        has_turn_cmd[i] = 1;
        printf("[server] Client %d TURN_CMD received\n", i);

        if (has_turn_cmd[0] && has_turn_cmd[1]) {
            // 両者のTURN_CMDを相手にOPPONENT_CMDとして転送
            uint8_t msg[1 + TURNCMD_WIRE_BYTES];

            msg[0] = MSG_OPPONENT_CMD;
            memcpy(msg + 1, turn_cmd[1], TURNCMD_WIRE_BYTES);
            send_all(client_sock[0], msg, sizeof(msg));

            msg[0] = MSG_OPPONENT_CMD;
            memcpy(msg + 1, turn_cmd[0], TURNCMD_WIRE_BYTES);
            send_all(client_sock[1], msg, sizeof(msg));

            has_turn_cmd[0] = 0;
            has_turn_cmd[1] = 0;
            printf("[server] TURN_CMD exchanged\n");
        }
        break;

    default:
        printf("[server] Unknown msg_type 0x%02x from client %d\n", msg_type, i);
        disconnect_client(i, mask);
        break;
    }
}

// 受信バッファからメッセージを切り出して処理
static void process_recv_buf(int i, fd_set *mask)
{
    while (recv_len[i] > 0) {
        uint8_t msg_type = recv_buf[i][0];
        int psize = msg_payload_size(msg_type);
        if (psize < 0) {
            printf("[server] Invalid msg_type 0x%02x from client %d\n", msg_type, i);
            disconnect_client(i, mask);
            return;
        }

        int total = 1 + psize; // header + payload
        if (recv_len[i] < total) break; // まだ足りない

        handle_message(i, msg_type, recv_buf[i] + 1, psize, mask);

        // 消費した分をシフト
        int remain = recv_len[i] - total;
        if (remain > 0) {
            memmove(recv_buf[i], recv_buf[i] + total, remain);
        }
        recv_len[i] = remain;
    }
}

int main(int argc, char *argv[])
{
    struct sockaddr_in addr;
    fd_set mask, readfds;
    int maxfd;
    int port = 12345;

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--port") == 0 || strcmp(argv[i], "-p") == 0) && i + 1 < argc) {
            port = atoi(argv[++i]);
        }
    }

    // ホスト名表示
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0)
        printf("[server] hostname: %s\n", hostname);

    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[server] bind");
        exit(1);
    }
    listen(listen_sock, MAX_CLIENTS);

    for (int i = 0; i < MAX_CLIENTS; i++) client_sock[i] = -1;
    connected = 0;

    FD_ZERO(&mask);
    FD_SET(listen_sock, &mask);
    maxfd = listen_sock + 1;

    reset_session();
    printf("[server] Listening on port %d\n", port);

    while (1) {
        readfds = mask;
        select(maxfd, &readfds, NULL, NULL, NULL);

        // 新規接続
        if (FD_ISSET(listen_sock, &readfds)) {
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sock[i] < 0) {
                    client_sock[i] = accept(listen_sock, NULL, NULL);
                    if (client_sock[i] < 0) break;

                    FD_SET(client_sock[i], &mask);
                    if (client_sock[i] + 1 > maxfd)
                        maxfd = client_sock[i] + 1;

                    connected++;
                    recv_len[i] = 0;
                    printf("[server] Client %d connected (total: %d)\n", i, connected);
                    break;
                }
            }
        }

        // クライアント受信
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (client_sock[i] < 0) continue;
            if (!FD_ISSET(client_sock[i], &readfds)) continue;

            int space = RECV_BUF_SIZE - recv_len[i];
            if (space <= 0) {
                printf("[server] Client %d recv buffer full\n", i);
                disconnect_client(i, &mask);
                continue;
            }

            ssize_t n = read(client_sock[i], recv_buf[i] + recv_len[i], space);
            if (n <= 0) {
                disconnect_client(i, &mask);
                continue;
            }

            recv_len[i] += (int)n;
            process_recv_buf(i, &mask);
        }
    }

    return 0;
}
