// net_client.c
#include "net_client.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/select.h>

static int sock = -1;
static int start_flag = 0;

void net_connect(const char* host, int port) {
    struct hostent* server;
    struct sockaddr_in addr;

    server = gethostbyname(host);
    if (!server) {
        perror("gethostbyname");
        return;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr.s_addr,
           server->h_addr_list[0],
           server->h_length);

    connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    printf("Connected to server\n");
}


void net_send_ready(void) {
    if (sock >= 0) {
        write(sock, "READY", 5);
        printf("SEND READY\n");
    }
}

void net_poll(void) {
    if (sock < 0) return;

    fd_set rfds;
    struct timeval tv = {0, 0};

    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);

    if (select(sock + 1, &rfds, NULL, NULL, &tv) > 0) {
        if (FD_ISSET(sock, &rfds)) {
            char buf[32] = {0};
            int n = read(sock, buf, sizeof(buf));
            if (n > 0 && strncmp(buf, "START", 5) == 0) {
                printf("RECV START\n");
                start_flag = 1;
            }
        }
    }
}

int net_received_start(void) {
    return start_flag;
}
