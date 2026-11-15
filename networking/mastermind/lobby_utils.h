#ifndef LOBBY_UTILS_H
#define LOBBY_UTILS_H

#include <stddef.h>
#include <stdint.h>

/* Returns 1 if the visible lobby roster changed (new player or name update). */
int update_lobby(const char *name, const char *ip, uint16_t port);

/* Removes peers silent for >= 5s. Returns 1 if any peer was removed. */
int prune_lobby(void);

/* Clears and redraws the lobby table while preserving current_input on the prompt line. */
void display_lobby(const char *current_input);

void set_lobby_message(const char *msg);

/* Returns 1 if a transient lobby message is currently active, 0 otherwise. */
int lobby_has_active_message(void);

int get_player_by_id(int target_id, char *out_ip, size_t out_ip_size, uint16_t *out_port);

#endif
