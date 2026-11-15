#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "state_controller.h"
#include "tcp_utils.h"
#include "lobby_utils.h"
#include "game_logic.h"
#include "logger.h"

static void close_peer_connection(struct app_context *ctx) {
    if (ctx->peer_sock >= 0) {
        close(ctx->peer_sock);
        ctx->peer_sock = -1;
    }
    ctx->fds[3].fd = -1;
}

static void return_to_lobby(struct app_context *ctx) {
    close_peer_connection(ctx);
    ctx->state = STATE_LOBBY;
    ctx->rx_len = 0;
    memset(ctx->rx_buffer, 0, sizeof(ctx->rx_buffer));
    ctx->peer_name[0] = '\0';
    ctx->last_tcp_ping = 0;
    cc_reset(&ctx->cc);
    prune_lobby();
    display_lobby(ctx->input_buf);
}

static void peer_disconnected(struct app_context *ctx) {
    log_event("Peer disconnected");
    if (ctx->peer_name[0] == '\0')
        snprintf(ctx->peer_name, sizeof(ctx->peer_name), "Peer");
    printf("\n%s disconnected. Press enter to go home.\n", ctx->peer_name);
    fflush(stdout);
    close_peer_connection(ctx);
    cc_reset(&ctx->cc);
    ctx->state = STATE_DISCONNECTED;
}

static void send_to_peer(struct app_context *ctx, const char *buf, int len) {
    if (ctx->cost_cutting) {
        char msg[CC_MAX_CHUNKS * CC_CHUNK_SIZE + 1];
        /* The chunk layer frames messages without trailing newlines. */
        int n = len > 0 ? len - 1 : 0;
        if (n > CC_MAX_CHUNKS * CC_CHUNK_SIZE)
            n = CC_MAX_CHUNKS * CC_CHUNK_SIZE;
        memcpy(msg, buf, n);
        msg[n] = '\0';
        cc_send_message(ctx->udp_sock, &ctx->peer_addr, &ctx->cc, msg);
    } else {
        send_all(ctx->peer_sock, buf, len);
    }
}

static void challenge_over_udp(struct app_context *ctx, const char *ip, uint16_t port) {
    char hello[48];

    memset(&ctx->peer_addr, 0, sizeof(ctx->peer_addr));
    ctx->peer_addr.sin_family = AF_INET;
    ctx->peer_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &ctx->peer_addr.sin_addr) <= 0) {
        set_lobby_message("Invalid peer address.");
        display_lobby(ctx->input_buf);
        return;
    }
    cc_reset(&ctx->cc);

    snprintf(hello, sizeof(hello), "challenge %s\n", ctx->my_name);
    send_to_peer(ctx, hello, (int)strlen(hello));
    ctx->state = STATE_WAITING_ACCEPT;
    ctx->last_activity = time(NULL);
    log_event("UDP challenge sent");
    printf("Request sent. Waiting for response...\n");
    fflush(stdout);
}

int handle_keyboard_input(struct app_context *ctx, char *cmd) {
    if (strcmp(cmd, "quit") == 0)
        return 0;

    if (ctx->state == STATE_LOBBY) {
        if (strncmp(cmd, "challenge ", 10) == 0) {
            int target_id = atoi(cmd + 10);
            char target_ip[16];
            uint16_t target_port;

            if (get_player_by_id(target_id, target_ip, sizeof(target_ip), &target_port)) {
                printf("Challenging %s:%d...\n", target_ip, target_port);
                if (ctx->cost_cutting) {
                    challenge_over_udp(ctx, target_ip, target_port);
                    return 1;
                }
                ctx->peer_sock = connect_to_peer(target_ip, target_port);
                if (ctx->peer_sock >= 0) {
                    ctx->fds[3].fd = ctx->peer_sock;
                    ctx->state = STATE_WAITING_ACCEPT;
                    ctx->last_activity = time(NULL);
                    ctx->last_tcp_ping = 0;
                    log_event("TCP connection to peer established (challenge sent)");
                    char hello[48];
                    snprintf(hello, sizeof(hello), "challenge %s\n", ctx->my_name);
                    send_to_peer(ctx, hello, (int)strlen(hello));
                    printf("Request sent. Waiting for response...\n");
                    fflush(stdout);
                } else {
                    set_lobby_message("Failed to connect.");
                    display_lobby(ctx->input_buf);
                }
            } else {
                set_lobby_message("Invalid player ID.");
                display_lobby(ctx->input_buf);
            }
        } else if (strlen(cmd) > 0) {
            set_lobby_message("Unknown command. Use 'challenge <id>' or 'quit'.");
            display_lobby(ctx->input_buf);
        } else {
            display_lobby(ctx->input_buf);
        }
    } else if (ctx->state == STATE_CHALLENGED) {
        if (strcmp(cmd, "yes") == 0) {
            char yes_msg[48];
            snprintf(yes_msg, sizeof(yes_msg), "yes %s\n", ctx->my_name);
            send_to_peer(ctx, yes_msg, (int)strlen(yes_msg));
            ctx->state = STATE_WAITING_OPPONENT;
            printf("Challenge accepted! You are the Codebreaker.\n");
            printf("Waiting for Mastermind to set the code...\n");
            fflush(stdout);
        } else if (strcmp(cmd, "no") == 0) {
            send_to_peer(ctx, "no\n", 3);
            set_lobby_message("Challenge rejected.");
            return_to_lobby(ctx);
        } else {
            printf("Accept challenge? (yes/no): ");
            fflush(stdout);
        }
    } else if (ctx->state == STATE_SETUP) {
        if (validate_sequence(cmd)) {
            memset(&ctx->game, 0, sizeof(ctx->game));
            snprintf(ctx->game.master_sequence, sizeof(ctx->game.master_sequence), "%s", cmd);
            ctx->game.is_mastermind = 1;
            send_to_peer(ctx, "start\n", 6);
            ctx->state = STATE_WAITING_OPPONENT;
            render_board(&ctx->game);
            printf("Waiting for codebreaker's first guess...\n");
            fflush(stdout);
        } else {
            printf("Invalid code. Must be 5 digits. Try again: ");
            fflush(stdout);
        }
    } else if (ctx->state == STATE_PLAYING_CODEBREAKER) {
        if (validate_sequence(cmd)) {
            snprintf(ctx->game.guesses[ctx->game.current_attempt],
                     sizeof(ctx->game.guesses[ctx->game.current_attempt]), "%s", cmd);
            char out_buf[8];
            snprintf(out_buf, sizeof(out_buf), "%s\n", cmd);
            send_to_peer(ctx, out_buf, (int)strlen(out_buf));
            ctx->state = STATE_WAITING_OPPONENT;
        } else {
            printf("Invalid guess. Must be 5 digits. Try again: ");
            fflush(stdout);
        }
    } else if (ctx->state == STATE_SENDING_FEEDBACK) {
        if (!validate_feedback(cmd)) {
            printf("Invalid feedback. Must be 5 characters of o, x or -. Try again: ");
            fflush(stdout);
            return 1;
        }
        int last = ctx->game.current_attempt;
        snprintf(ctx->game.feedbacks[last], sizeof(ctx->game.feedbacks[last]), "%s", cmd);
        ctx->game.current_attempt++;
        render_board(&ctx->game);

        char out_buf[8];
        snprintf(out_buf, sizeof(out_buf), "%s\n", ctx->game.feedbacks[last]);
        send_to_peer(ctx, out_buf, (int)strlen(out_buf));
        ctx->state = STATE_WAITING_OPPONENT;

        if (strcmp(ctx->game.feedbacks[last], "xxxxx") == 0 || ctx->game.current_attempt == 12) {
            char master_msg[8];
            snprintf(master_msg, sizeof(master_msg), "%s\n", ctx->game.master_sequence);
            send_to_peer(ctx, master_msg, (int)strlen(master_msg));
            close_peer_connection(ctx);
            ctx->state = STATE_GAME_OVER;
            printf("Game over! Press enter to go home.\n");
            fflush(stdout);
        } else {
            printf("Waiting for codebreaker's next guess...\n");
            fflush(stdout);
        }
    } else if (ctx->state == STATE_GAME_OVER) {
        if (ctx->game.master_sequence[0] != '\0' && cc_pending(&ctx->cc) == 0)
            return_to_lobby(ctx);
    } else if (ctx->state == STATE_DISCONNECTED) {
        return_to_lobby(ctx);
    }

    return 1;
}

static void process_game_command(struct app_context *ctx, char *buf) {
    if (strcmp(buf, "ping") == 0)
        return;

    if (ctx->state == STATE_LOBBY || ctx->state == STATE_CHALLENGED) {
        if (strncmp(buf, "challenge ", 10) == 0) {
            snprintf(ctx->peer_name, sizeof(ctx->peer_name), "%s", buf + 10);
            if (ctx->state == STATE_LOBBY) {
                /* Cost-cutting mode enters STATE_CHALLENGED on the challenge packet itself. */
                ctx->state = STATE_CHALLENGED;
                printf("\nIncoming challenge from %s! Accept? (yes/no): ", ctx->peer_name);
                fflush(stdout);
            }
        }
    } else if (ctx->state == STATE_WAITING_ACCEPT) {
        if (strncmp(buf, "yes ", 4) == 0) {
            snprintf(ctx->peer_name, sizeof(ctx->peer_name), "%s", buf + 4);
            printf("Challenge accepted! You are the Mastermind.\n");
            printf("Enter your secret 5-digit code: ");
            fflush(stdout);
            ctx->state = STATE_SETUP;
        } else {
            set_lobby_message("Your challenge was rejected.");
            return_to_lobby(ctx);
        }
    } else if (ctx->state == STATE_WAITING_OPPONENT) {
        if (strcmp(buf, "start") == 0) {
            memset(&ctx->game, 0, sizeof(ctx->game));
            ctx->game.is_mastermind = 0;
            ctx->state = STATE_PLAYING_CODEBREAKER;
            render_board(&ctx->game);
            printf("Enter your 5-digit guess: ");
            fflush(stdout);
        } else if (ctx->game.is_mastermind == 1) {
            if (validate_sequence(buf)) {
                snprintf(ctx->game.guesses[ctx->game.current_attempt],
                         sizeof(ctx->game.guesses[ctx->game.current_attempt]), "%s", buf);
                printf("\nCodebreaker guessed %s. Enter feedback (5 characters of o, x, -): ", buf);
                fflush(stdout);
                ctx->state = STATE_SENDING_FEEDBACK;
            } else {
                printf("Invalid attempt received from peer, ignoring it.\n");
            }
        } else if (ctx->game.is_mastermind == 0 && validate_feedback(buf)) {
            snprintf(ctx->game.feedbacks[ctx->game.current_attempt],
                     sizeof(ctx->game.feedbacks[ctx->game.current_attempt]), "%s", buf);
            ctx->game.current_attempt++;
            render_board(&ctx->game);

            if (strcmp(buf, "xxxxx") == 0) {
                printf("Code cracked! You win! Waiting for the master sequence...\n");
                ctx->state = STATE_GAME_OVER;
            } else if (ctx->game.current_attempt == 12) {
                printf("Out of attempts! You lose. Waiting for the master sequence...\n");
                ctx->state = STATE_GAME_OVER;
            } else {
                ctx->state = STATE_PLAYING_CODEBREAKER;
                printf("Enter your 5-digit guess: ");
                fflush(stdout);
            }
        } else if (ctx->game.is_mastermind == 0) {
            printf("Invalid feedback received from peer, ignoring it.\n");
        }
    } else if (ctx->state == STATE_GAME_OVER) {
        if (ctx->game.is_mastermind == 0 && ctx->game.master_sequence[0] == '\0' && validate_sequence(buf)) {
            snprintf(ctx->game.master_sequence, sizeof(ctx->game.master_sequence), "%s", buf);
            render_board(&ctx->game);
            close_peer_connection(ctx);
            printf("Game over! Press enter to go home.\n");
            fflush(stdout);
        }
    }
}

void handle_peer_data(struct app_context *ctx) {
    if (ctx->rx_len >= (int)sizeof(ctx->rx_buffer) - 1) {
        peer_disconnected(ctx);
        return;
    }

    int bytes = (int)recv(ctx->peer_sock, ctx->rx_buffer + ctx->rx_len,
                          sizeof(ctx->rx_buffer) - ctx->rx_len - 1, 0);
    if (bytes <= 0) {
        peer_disconnected(ctx);
        return;
    }

    ctx->rx_len += bytes;
    ctx->rx_buffer[ctx->rx_len] = '\0';
    ctx->last_activity = time(NULL);

    char *newline;
    while ((newline = strchr(ctx->rx_buffer, '\n')) != NULL) {
        *newline = '\0';

        char cmd[256];
        snprintf(cmd, sizeof(cmd), "%s", ctx->rx_buffer);
        cmd[strcspn(cmd, "\r")] = 0;

        int remaining = ctx->rx_len - (int)(newline - ctx->rx_buffer + 1);
        memmove(ctx->rx_buffer, newline + 1, remaining);
        ctx->rx_len = remaining;
        ctx->rx_buffer[remaining] = '\0';

        char log_msg[300];
        snprintf(log_msg, sizeof(log_msg), "TCP received: %s", cmd);
        log_event(log_msg);

        process_game_command(ctx, cmd);
    }
}

void handle_udp_peer_data(struct app_context *ctx) {
    unsigned char pkt[16];
    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);
    char line[CC_MAX_CHUNKS * CC_CHUNK_SIZE + 1];

    int n = (int)recvfrom(ctx->udp_sock, pkt, sizeof(pkt), 0,
                          (struct sockaddr *)&sender, &sender_len);
    if (n <= 0)
        return;

    if (ctx->state != STATE_LOBBY) {
        if (sender.sin_addr.s_addr != ctx->peer_addr.sin_addr.s_addr ||
            sender.sin_port != ctx->peer_addr.sin_port)
            return;
    }

    if (cc_process_packet(ctx->udp_sock, &sender, &ctx->cc, pkt, n, line)) {
        if (ctx->state == STATE_LOBBY) {
            if (strncmp(line, "challenge ", 10) != 0) {
                cc_reset(&ctx->cc);
                return;
            }
            ctx->peer_addr = sender;
        }
        ctx->last_activity = time(NULL);

        char log_msg[300];
        snprintf(log_msg, sizeof(log_msg), "UDP received: %s", line);
        log_event(log_msg);

        process_game_command(ctx, line);
    }
}

void check_turn_timeout(struct app_context *ctx) {
    if (!ctx->cost_cutting) {
        if (ctx->peer_sock < 0)
            return;
        time_t now = time(NULL);
        if (now - ctx->last_tcp_ping >= 1) {
            send_to_peer(ctx, "ping\n", 5);
            ctx->last_tcp_ping = now;
        }
        if (now - ctx->last_activity >= 5)
            peer_disconnected(ctx);
        return;
    }

    if (ctx->state == STATE_LOBBY || ctx->state == STATE_DISCONNECTED)
        return;
    if (ctx->state == STATE_GAME_OVER && cc_pending(&ctx->cc) == 0)
        return;

    if (!cc_tick(ctx->udp_sock, &ctx->peer_addr, &ctx->cc))
        peer_disconnected(ctx);
}
