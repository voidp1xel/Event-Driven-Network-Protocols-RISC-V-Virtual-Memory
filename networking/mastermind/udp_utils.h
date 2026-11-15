#ifndef UDP_UTILS_H
#define UDP_UTILS_H

#include <stdint.h>

int setup_udp_socket();
void send_broadcast(int udp_sock, const char *name, uint16_t tcp_port);
int process_incoming_broadcast(int udp_sock);

#endif