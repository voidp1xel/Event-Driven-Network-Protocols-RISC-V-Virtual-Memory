#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "udp_utils.h"
#include "game_protocol.h"
#include "lobby_utils.h"
#include "logger.h"

/* Track our own advertised port so we can filter out self-broadcasts. */
static uint16_t my_port = 0;

/* bind() succeeds only for an IP assigned to a local interface on this host. */
static int is_my_address(const char *ip) {
    int test_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (test_sock < 0)
        return 0;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(0);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) <= 0) {
        close(test_sock);
        return 0;
    }

    int mine = (bind(test_sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    close(test_sock);
    return mine;
}

int setup_udp_socket(void) {
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("UDP socket failed");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_BROADCAST failed");
        exit(1);
    }

    /* Allow multiple local instances on the same machine to share BROADCAST_PORT. */
    if (setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR failed");
        exit(1);
    }

    struct sockaddr_in recv_addr;
    memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(BROADCAST_PORT);
    recv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(udp_sock, (struct sockaddr *)&recv_addr, sizeof(recv_addr)) < 0) {
        perror("UDP bind failed");
        exit(1);
    }

    return udp_sock;
}

void send_broadcast(int udp_sock, const char *name, uint16_t tcp_port) {
    struct discovery_payload payload;
    memset(&payload, 0, sizeof(payload));
    my_port = tcp_port;
    memcpy(payload.magic, MAGIC_PACKET, 4);
    payload.tcp_port = htons(tcp_port);
    snprintf(payload.player_name, sizeof(payload.player_name), "%s", name);

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(BROADCAST_PORT);
    dest_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    sendto(udp_sock, &payload, sizeof(payload), 0,
           (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    char log_msg[128];
    snprintf(log_msg, sizeof(log_msg), "UDP broadcast sent: name=%.31s tcp_port=%d", name, tcp_port);
    log_event(log_msg);
}

int process_incoming_broadcast(int udp_sock) {
    struct discovery_payload recv_payload;
    struct sockaddr_in sender_addr;
    socklen_t sender_len = sizeof(sender_addr);

    int n = recvfrom(udp_sock, &recv_payload, sizeof(recv_payload), 0,
                     (struct sockaddr *)&sender_addr, &sender_len);
    if (n != (int)sizeof(struct discovery_payload))
        return 0;

    if (memcmp(recv_payload.magic, MAGIC_PACKET, 4) != 0)
        return 0;

    recv_payload.player_name[sizeof(recv_payload.player_name) - 1] = '\0';

    char ip_str[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &sender_addr.sin_addr, ip_str, sizeof(ip_str)))
        return 0;

    uint16_t peer_port = ntohs(recv_payload.tcp_port);
    int changed = 0;

    if (!(peer_port == my_port && is_my_address(ip_str)))
        changed = update_lobby(recv_payload.player_name, ip_str, peer_port);

    char log_msg[128];
    snprintf(log_msg, sizeof(log_msg), "UDP broadcast received: name=%.31s ip=%s tcp_port=%d",
             recv_payload.player_name, ip_str, peer_port);
    log_event(log_msg);
    return changed;
}
