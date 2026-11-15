#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "tcp_utils.h"
#include "logger.h"

int setup_tcp_listener(uint16_t *assigned_port){
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0){
        perror("TCP socket failed");
        exit(1);
    }

    int opt = 1;
    /* Allow immediate port reuse on rapid restarts. */
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        perror("setsockopt failed");

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* Bind the first available port in the 8000-8099 range. */
    uint16_t port = 8000;
    addr.sin_port = htons(port);
    while(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        port++;
        if(port > 8099){
            perror("TCP bind failed");
            exit(1);
        }
        addr.sin_port = htons(port);
    }
    *assigned_port = port;

    if(listen(sock, 5) < 0){
        perror("TCP listen failed");
        exit(1);
    }

    return sock;
}

int connect_to_peer(const char *ip, uint16_t port){
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }

    if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        close(sock);
        return -1;
    }
    return sock;
}

/* Loop over send() until all len bytes are written to the socket. */
int send_all(int sock, const char *buf, int len){
    int total = 0;
    int n = 1;
    char log_msg[128];

    /* Protocol messages end with '\n'; strip it when logging. */
    int log_len = len > 0 ? len - 1 : 0;
    snprintf(log_msg, sizeof(log_msg), "TCP sent: %.*s", log_len, buf);
    log_event(log_msg);

    while(total < len){
        n = send(sock, buf + total, len - total, 0);
        if (n <= 0) break;
        total += n;
    }

    return (n <= 0) ? -1 : 0;
}