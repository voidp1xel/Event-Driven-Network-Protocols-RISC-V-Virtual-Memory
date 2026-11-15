#ifndef COST_CUTTING_H
#define COST_CUTTING_H

#include <stdint.h>
#include <netinet/in.h>

/*
 * Reliable fragmented-UDP wire protocol (--cost-cutting):
 *   DATA -> [ 'D', msg_id, seq, total, byte0, byte1 ] (6 bytes fixed)
 *   ACK  -> [ 'A', msg_id, seq ]                      (3 bytes)
 *   PING -> [ 'P' ]                                   (1 byte keepalive)
 */
#define CC_PKT_DATA 'D'
#define CC_PKT_ACK  'A'
#define CC_PKT_PING 'P'
#define CC_PKT_SIZE 6

#define CC_CHUNK_SIZE 2   /* Payload bytes per DATA packet */
#define CC_MAX_CHUNKS 32  /* Maximum chunks per message (64 bytes max) */
#define CC_MAX_QUEUE  8   /* Maximum concurrent in-flight outgoing messages */
#define CC_RETRY_MS   100 /* Per-chunk retransmission timeout in milliseconds */
#define CC_PING_MS    1000
#define CC_TIMEOUT_MS 3000

/* Outgoing fragmented message awaiting per-chunk ACKs. */
struct cc_out_msg{
    int active;
    int msg_id;
    int total;
    int acked[CC_MAX_CHUNKS];
    char data[CC_MAX_CHUNKS][CC_CHUNK_SIZE];
    long last_sent_ms[CC_MAX_CHUNKS];
};

struct cc_state{
    struct cc_out_msg out[CC_MAX_QUEUE];
    int next_msg_id;      /* Sequence ID assigned to the next outgoing message */
    int expected_msg_id;  /* Sequence ID of the next expected incoming message */
    int in_total;         /* Total chunk count for the message being reassembled */
    int in_got[CC_MAX_CHUNKS];
    char in_data[CC_MAX_CHUNKS][CC_CHUNK_SIZE];
    long last_heard_ms;   /* Timestamp of the last packet received from the peer */
    long last_ping_ms;
};

int setup_udp_game_socket(uint16_t *assigned_port);
void cc_reset(struct cc_state *cc);
int cc_send_message(int sock, struct sockaddr_in *dest, struct cc_state *cc, const char *msg);
int cc_process_packet(int sock, struct sockaddr_in *sender, struct cc_state *cc,
                      const unsigned char *pkt, int n, char *out_line);
int cc_tick(int sock, struct sockaddr_in *dest, struct cc_state *cc);
int cc_pending(struct cc_state *cc);

#endif