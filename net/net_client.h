// net_client.h
#ifndef NET_CLIENT_H
#define NET_CLIENT_H

void net_connect(const char* host, int port);
void net_send_ready(void);
void net_poll(void);
int net_received_start(void);

#endif
