#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include "cost_cutting.h"
#include "logger.h"

static long now_ms(){
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000L + tv.tv_usec / 1000;
}

/* Bind a UDP game socket in the 8000-8099 range and advertise its port via LAN discovery. */
int setup_udp_game_socket(uint16_t *assigned_port) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0){
        perror("UDP game socket failed");
        exit(1);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    /* Probe ports 8000..8099 sequentially without SO_REUSEADDR so each peer binds a unique port. */
    uint16_t port = 8000;
    addr.sin_port = htons(port);
    while(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        port++;
        if(port > 8099){
            perror("UDP game bind failed");
            exit(1);
        }
        addr.sin_port = htons(port);
    }

    *assigned_port = port;
    return sock;
}

void cc_reset(struct cc_state *cc){
    memset(cc, 0, sizeof(*cc));
    cc->last_heard_ms = now_ms();
}

/* Serialize and transmit a single 6-byte DATA chunk. */
static void send_chunk(int sock, struct sockaddr_in *dest, struct cc_out_msg *m, int seq){
    unsigned char pkt[CC_PKT_SIZE];
    pkt[0] = CC_PKT_DATA;
    pkt[1] = m->msg_id;
    pkt[2] = seq;
    pkt[3] = m->total;
    pkt[4] = m->data[seq][0];
    pkt[5] = m->data[seq][1];
    sendto(sock, pkt, CC_PKT_SIZE, 0, (struct sockaddr*)dest, sizeof(*dest));
    m->last_sent_ms[seq] = now_ms();
}

/* Fragment msg into 2-byte payload chunks and transmit all chunks immediately. */
int cc_send_message(int sock, struct sockaddr_in *dest, struct cc_state *cc, const char *msg){
    int len = strlen(msg);
    int total = (len + CC_CHUNK_SIZE - 1) / CC_CHUNK_SIZE;
    char log_msg[128];

    if(total < 1 || total > CC_MAX_CHUNKS || cc->next_msg_id > 255)
        return -1;

    /* Locate a free outgoing message slot. */
    int slot = -1;
    for(int i = 0; i < CC_MAX_QUEUE; i++){
        if(!cc->out[i].active) {
            slot = i;
            break;
        }
    }
    if(slot < 0)
        return -1;

    struct cc_out_msg *m = &cc->out[slot];
    memset(m, 0, sizeof(*m));
    m->active = 1;
    m->msg_id = cc->next_msg_id;
    cc->next_msg_id++;
    m->total = total;

    /* Populate chunk payloads, zero-padding the final chunk if needed. */
    for(int c = 0; c < total; c++){
        for(int k = 0; k < CC_CHUNK_SIZE; k++){
            int idx = c * CC_CHUNK_SIZE + k;
            if(idx < len)
                m->data[c][k] = msg[idx];
            else
                m->data[c][k] = '\0';
        }
    }

    for(int c = 0; c < total; c++){
        send_chunk(sock, dest, m, c);
        snprintf(log_msg, sizeof(log_msg), "Cost-Cutting: Sent fragmented chunk %d/%d of message %d: %.2s",
                 c + 1, total, m->msg_id, m->data[c]);
        log_event(log_msg);
    }
    return 0;
}

/* Process an incoming DATA, ACK, or PING packet; returns 1 when a complete message is reassembled. */
int cc_process_packet(int sock, struct sockaddr_in *sender, struct cc_state *cc,
                      const unsigned char *pkt, int n, char *out_line){
    char log_msg[128];
    cc->last_heard_ms = now_ms();

    if(n == 1 && pkt[0] == CC_PKT_PING){
        log_event("Cost-Cutting: Received heartbeat");
        return 0;
    }

    if(n == 3 && pkt[0] == CC_PKT_ACK){
        int id = pkt[1];
        int seq = pkt[2];
        for(int i = 0; i < CC_MAX_QUEUE; i++){
            struct cc_out_msg *m = &cc->out[i];
            if(m->active && m->msg_id == id && seq < m->total){
                m->acked[seq] = 1;
                snprintf(log_msg, sizeof(log_msg), "Cost-Cutting: Received valid ACK for chunk %d of message %d",
                         seq, id);
                log_event(log_msg);

                /* Mark the outgoing message slot inactive once all chunks are acknowledged. */
                int all_acked = 1;
                for(int c = 0; c < m->total; c++){
                    if(!m->acked[c])
                        all_acked = 0;
                }
                if(all_acked)
                    m->active = 0;
            }
        }
        return 0;
    }

    if(n == CC_PKT_SIZE && pkt[0] == CC_PKT_DATA){
        int id = pkt[1];
        int seq = pkt[2];
        int total = pkt[3];

        if(total < 1 || total > CC_MAX_CHUNKS || seq >= total)
            return 0;

        /* Drop future messages until expected_msg_id is fully reassembled; sender will retransmit. */
        if(id > cc->expected_msg_id)
            return 0;

        /* Acknowledge accepted chunks (including duplicates in case a prior ACK was dropped). */
        unsigned char ack[3];
        ack[0] = CC_PKT_ACK;
        ack[1] = pkt[1];
        ack[2] = pkt[2];
        sendto(sock, ack, 3, 0, (struct sockaddr*)sender, sizeof(*sender));
        snprintf(log_msg, sizeof(log_msg), "Cost-Cutting: Sent ACK for chunk %d of message %d", seq, id);
        log_event(log_msg);

        /* Ignore duplicate chunks from an already-delivered message. */
        if(id < cc->expected_msg_id)
            return 0;

        /* Reset reassembly state when the first chunk of a new message arrives. */
        if(cc->in_total != total){
            memset(cc->in_got, 0, sizeof(cc->in_got));
            cc->in_total = total;
        }

        /* Buffer chunk at its sequence index to support out-of-order arrival. */
        if(!cc->in_got[seq]){
            cc->in_got[seq] = 1;
            cc->in_data[seq][0] = pkt[4];
            cc->in_data[seq][1] = pkt[5];
            log_event("Cost-Cutting: Buffered incoming chunk");
        }

        /* Check whether all chunks for the current message have arrived. */
        for(int c = 0; c < total; c++){
            if (!cc->in_got[c])
                return 0;
        }

        /* Reassemble chunks in sequence order. */
        for(int c = 0; c < total; c++){
            for (int k = 0; k < CC_CHUNK_SIZE; k++)
                out_line[c * CC_CHUNK_SIZE + k] = cc->in_data[c][k];
        }
        out_line[total * CC_CHUNK_SIZE] = '\0';

        cc->expected_msg_id++;
        cc->in_total = 0;
        memset(cc->in_got, 0, sizeof(cc->in_got));
        snprintf(log_msg, sizeof(log_msg), "Cost-Cutting: Message fully reassembled: %.64s", out_line);
        log_event(log_msg);
        return 1;
    }

    return 0;
}

/* Periodic timer tick: retransmit unacknowledged chunks after 100 ms, send 1 s heartbeats, and check 3 s peer timeout. */
int cc_tick(int sock, struct sockaddr_in *dest, struct cc_state *cc){
    long now = now_ms();
    char log_msg[128];

    for(int i = 0; i < CC_MAX_QUEUE; i++){
        struct cc_out_msg *m = &cc->out[i];
        if(!m->active)
            continue;
        for(int c = 0; c < m->total; c++) {
            if(!m->acked[c] && now - m->last_sent_ms[c] >= CC_RETRY_MS){
                send_chunk(sock, dest, m, c);
                snprintf(log_msg, sizeof(log_msg),
                    "Cost-Cutting: ACK timeout, retransmitting chunk %d of message %d", c, m->msg_id);
                log_event(log_msg);
            }
        }
    }

    if(now - cc->last_ping_ms >= CC_PING_MS){
        unsigned char ping[1];
        ping[0] = CC_PKT_PING;
        sendto(sock, ping, 1, 0, (struct sockaddr*)dest, sizeof(*dest));
        cc->last_ping_ms = now;
        log_event("Cost-Cutting: Sent heartbeat");
    }

    if(now - cc->last_heard_ms >= CC_TIMEOUT_MS)
        return 0;
    return 1;
}

/* Return the number of outgoing messages still awaiting full acknowledgement. */
int cc_pending(struct cc_state *cc){
    int count = 0;
    for(int i = 0; i < CC_MAX_QUEUE; i++){
        if(cc->out[i].active)
            count++;
    }
    return count;
}