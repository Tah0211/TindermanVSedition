#include "net_client.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/select.h>

static int sock = -1;
static int start_flag = 0;

/*
 * サーバへ接続
 */
void net_connect(const char* host, int port)
{
    struct hostent* server;
    struct sockaddr_in addr;

    printf("[net] net_connect called\n");

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
    memcpy(&addr.sin_addr.s_addr,
           server->h_addr_list[0],
           server->h_length);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[net] connect");
        close(sock);
        sock = -1;
        return;
    }

    printf("[net] connected to server\n");
}

/*
 * READY を送信
 */
void net_send_ready(void)
{
    if (sock < 0) {
        fprintf(stderr, "[net] socket not connected\n");
        return;
    }

    const char* msg = "READY";
    ssize_t n = write(sock, msg, strlen(msg));

    if (n < 0) {
        perror("[net] write");
    } else if (n != (ssize_t)strlen(msg)) {
        fprintf(stderr, "[net] partial write (%zd bytes)\n", n);
    } else {
        printf("[net] SEND READY\n");
    }
}

/*
 * 非同期受信（毎フレーム呼ぶ）
 */
void net_poll(void)
{
    if (sock < 0) return;

    fd_set rfds;
    struct timeval tv;

    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);

    tv.tv_sec = 0;
    tv.tv_usec = 0;

    int ret = select(sock + 1, &rfds, NULL, NULL, &tv);
    if (ret <= 0) return;

    if (FD_ISSET(sock, &rfds)) {
        char buf[32] = {0};
        ssize_t n = read(sock, buf, sizeof(buf) - 1);

        if (n <= 0) {
            perror("[net] read");
            close(sock);
            sock = -1;
            return;
        }

        printf("[net] RECV: %s\n", buf);

        if (strncmp(buf, "START", 5) == 0) {
            start_flag = 1;
        }
    }
}

/*
 * START を受信したか
 */
int net_received_start(void)
{
    return start_flag;
}
