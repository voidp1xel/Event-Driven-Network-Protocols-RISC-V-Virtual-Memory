#ifndef GAME_PROTOCOL_H
#define GAME_PROTOCOL_H

#include <stdint.h>

#define MAGIC_PACKET "MIND"
#define BROADCAST_PORT 9000

/* Packed 38-byte UDP broadcast discovery payload (no compiler padding). */
struct __attribute__((packed)) discovery_payload {
    char magic[4];
    uint16_t tcp_port;
    char player_name[32];
};

/*
 * Application-layer protocol messages exchanged between peers:
 *   challenge <name>  Challenger -> Challenged: initiates a match request
 *   yes <name>        Challenged -> Challenger: accepts and names the Codebreaker
 *   no                Challenged -> Challenger: declines the challenge
 *   start             Mastermind -> Codebreaker: confirms the 5-digit secret is set
 *   <5 digits>        Codebreaker -> Mastermind: guess attempt (or Mastermind reveal at game end)
 *   <5 chars o/x/->   Mastermind -> Codebreaker: positional feedback for the latest guess
 *   ping              Bidirectional TCP keepalive sent every 1s (5s inactivity timeout)
 *
 * Over TCP, messages are newline-delimited ('\n'). Under --cost-cutting, messages
 * are segmented into 6-byte UDP packets (see cost_cutting.h).
 */

#define VERB_CHALLENGE "challenge"
#define VERB_YES "yes"
#define VERB_NO "no"
#define VERB_START "start"

#endif
