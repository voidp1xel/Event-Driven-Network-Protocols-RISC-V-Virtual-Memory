#ifndef STATE_CONTROLLER_H
#define STATE_CONTROLLER_H

#include <poll.h>
#include <stddef.h>
#include <time.h>
#include <netinet/in.h>
#include "board_ui.h"
#include "cost_cutting.h"

#define STATE_LOBBY 0
#define STATE_CHALLENGED 1
#define STATE_WAITING_ACCEPT 2
#define STATE_SETUP 3
#define STATE_PLAYING_CODEBREAKER 4
#define STATE_WAITING_OPPONENT 5
#define STATE_GAME_OVER 6
#define STATE_DISCONNECTED 7
#define STATE_SENDING_FEEDBACK 8

struct app_context {
    int state;
    int peer_sock;
    struct pollfd *fds;
    struct game_state game;
    time_t last_activity;
    time_t last_tcp_ping;
    char rx_buffer[512];
    int rx_len;
    char input_buf[256];
    size_t input_len;
    char my_name[32];
    char peer_name[32];
    int cost_cutting;
    int udp_sock;
    struct sockaddr_in peer_addr;
    struct cc_state cc;
};

int handle_keyboard_input(struct app_context *ctx, char *cmd);
void handle_peer_data(struct app_context *ctx);
void handle_udp_peer_data(struct app_context *ctx);
void check_turn_timeout(struct app_context *ctx);

#endif
