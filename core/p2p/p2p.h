#ifndef P2P_H
#define P2P_H

#include <stdint.h>
#include <stddef.h>

typedef struct P2PNode P2PNode;

P2PNode* p2p_create(uint16_t port);
int p2p_start(P2PNode* node);

const uint8_t* p2p_get_public_key(P2PNode* node);

int p2p_set_peer_key(P2PNode* node, const uint8_t* peer_pubkey);

int p2p_send(P2PNode* node,
             const char* ip,
             uint16_t port,
             const uint8_t* data,
             size_t len);

typedef void (*p2p_on_message)(
    const char* ip,
    uint16_t port,
    const uint8_t* data,
    size_t len
);

void p2p_set_callback(P2PNode* node, p2p_on_message cb);
void p2p_destroy(P2PNode* node);

#endif