#ifndef TCP_UTILS_H
#define TCP_UTILS_H

#include <stdint.h>

int setup_tcp_listener(uint16_t *assigned_port);
int connect_to_peer(const char *ip, uint16_t port);
int send_all(int sock, const char *buf, int len);

#endif