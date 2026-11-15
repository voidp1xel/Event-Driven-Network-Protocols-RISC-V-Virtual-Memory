#ifndef NETWORK_UTILS_H
#define NETWORK_UTILS_H

void die(const char *msg);
int connect_to_server(const char *hostname, const char *port);
void start_timer(int seconds);
void wait_for_socket(int sockfd, short events);
void send_all(int sockfd, const char *buf, int len);

#endif