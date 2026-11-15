#include <stdio.h>
#include <string.h>
#include <time.h>
#include "lobby_utils.h"

#define MAX_PLAYERS 50
#define PEER_TIMEOUT_SEC 5
#define LOBBY_MSG_TTL_SEC 5

struct player_info {
    int active;
    char name[32];
    char ip[16];
    uint16_t port;
    time_t last_seen;
};

static struct player_info players[MAX_PLAYERS] = {0};

/* Transient status message displayed below the roster for LOBBY_MSG_TTL_SEC. */
static char lobby_msg[128] = "";
static time_t lobby_msg_time = 0;

void set_lobby_message(const char *msg) {
    snprintf(lobby_msg, sizeof(lobby_msg), "%s", msg);
    lobby_msg_time = time(NULL);
}

int lobby_has_active_message(void) {
    if (lobby_msg[0] == '\0')
        return 0;
    if (time(NULL) - lobby_msg_time >= LOBBY_MSG_TTL_SEC) {
        lobby_msg[0] = '\0';
        return 0;
    }
    return 1;
}

int update_lobby(const char *name, const char *ip, uint16_t port) {
    time_t now = time(NULL);

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].active && strcmp(players[i].ip, ip) == 0 && players[i].port == port) {
            players[i].last_seen = now;
            if (strncmp(players[i].name, name, sizeof(players[i].name) - 1) != 0) {
                snprintf(players[i].name, sizeof(players[i].name), "%s", name);
                return 1;
            }
            return 0;
        }
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (!players[i].active) {
            players[i].active = 1;
            snprintf(players[i].name, sizeof(players[i].name), "%s", name);
            snprintf(players[i].ip, sizeof(players[i].ip), "%s", ip);
            players[i].port = port;
            players[i].last_seen = now;
            return 1;
        }
    }

    return 0;
}

int prune_lobby(void) {
    time_t now = time(NULL);
    int removed = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].active && now - players[i].last_seen >= PEER_TIMEOUT_SEC) {
            players[i].active = 0;
            removed = 1;
        }
    }
    return removed;
}

void display_lobby(const char *current_input) {
    /* Clear screen and home cursor so roster updates don't scroll endlessly. */
    printf("\x1b[H\x1b[2J");
    printf("Players Online:\n");
    printf("%-4s %-16s %-15s %-6s %s\n", "ID", "Name", "IP Addr", "Port", "Last Seen");

    int id = 1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].active) {
            char time_buf[16];
            struct tm *tm_info = localtime(&players[i].last_seen);
            if (tm_info)
                strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);
            else
                snprintf(time_buf, sizeof(time_buf), "online");

            printf("%-4d %-16s %-15s %-6d %s\n",
                   id++, players[i].name, players[i].ip, players[i].port, time_buf);
        }
    }
    printf("---------------------------------------------------\n");
    if (lobby_has_active_message())
        printf("%s\n", lobby_msg);

    printf("> %s", current_input ? current_input : "");
    fflush(stdout);
}

int get_player_by_id(int target_id, char *out_ip, size_t out_ip_size, uint16_t *out_port) {
    int current_id = 1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].active) {
            if (current_id == target_id) {
                snprintf(out_ip, out_ip_size, "%s", players[i].ip);
                *out_port = players[i].port;
                return 1;
            }
            current_id++;
        }
    }
    return 0;
}
