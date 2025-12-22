// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/select.h>

#define MAX_CLIENTS 2
#define BUF_SIZE 64

int main(int argc, char *argv[]) {
    int listen_sock, client_sock[MAX_CLIENTS];
    struct sockaddr_in addr;
    fd_set mask, readfds;
    int maxfd, i;

    int ready[MAX_CLIENTS] = {0, 0};
    int connected = 0;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listen_sock = socket(AF_INET, SOCK_STREAM, 0);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[1]));
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(listen_sock, MAX_CLIENTS);

    for (i = 0; i < MAX_CLIENTS; i++) client_sock[i] = -1;

    FD_ZERO(&mask);
    FD_SET(listen_sock, &mask);
    maxfd = listen_sock + 1;

    printf("Server started\n");

    while (1) {
        readfds = mask;
        select(maxfd, &readfds, NULL, NULL, NULL);

        /* 新規接続 */
        if (FD_ISSET(listen_sock, &readfds)) {
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (client_sock[i] < 0) {
                    client_sock[i] = accept(listen_sock, NULL, NULL);
                    FD_SET(client_sock[i], &mask);
                    if (client_sock[i] + 1 > maxfd)
                        maxfd = client_sock[i] + 1;
                    printf("Client %d connected\n", i);
                    connected++;
                    break;
                }
            }
        }

        /* クライアント受信 */
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (client_sock[i] >= 0 && FD_ISSET(client_sock[i], &readfds)) {
                char buf[BUF_SIZE] = {0};
                int n = read(client_sock[i], buf, BUF_SIZE);

                if (n <= 0) {
                    close(client_sock[i]);
                    FD_CLR(client_sock[i], &mask);
                    client_sock[i] = -1;
                    ready[i] = 0;
                    connected--;
                    printf("Client %d disconnected\n", i);
                } else {
                    printf("Recv from %d: %s\n", i, buf);

                    if (strncmp(buf, "READY", 5) == 0) {
                        ready[i] = 1;
                        printf("Client %d READY\n", i);
                    }

                    if (connected == 2 && ready[0] && ready[1]) {
                        printf("Both READY -> START\n");
                        for (int j = 0; j < MAX_CLIENTS; j++) {
                            write(client_sock[j], "START", 5);
                            ready[j] = 0;
                        }
                    }
                }
            }
        }
    }
}
