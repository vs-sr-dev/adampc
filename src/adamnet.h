/* adamnet.h - the AdamNet serial bus: one half-duplex wire shared by every 6801.
 * Modelled at frame level: each SCI transmission becomes a frame (start/end time);
 * every node with its receiver enabled gets it when its own clock reaches the frame end,
 * the sender included (the wire echoes back).
 */
#ifndef ADAMNET_H
#define ADAMNET_H
#include <stdint.h>
#include "m6801.h"

#define NET_MAX_NODES  8
#define NET_FRAMES     512

typedef struct { uint64_t start, end; uint8_t byte; uint8_t src; } NetFrame;

typedef struct AdamNet AdamNet;
typedef struct {
    AdamNet *net;
    M6801 *cpu;
    unsigned cursor;          /* next frame index to deliver */
    const char *name;
    int index;
} NetNode;

struct AdamNet {
    NetFrame frame[NET_FRAMES];
    unsigned count;           /* frames ever sent (ring index = count % NET_FRAMES) */
    NetNode node[NET_MAX_NODES];
    int nnodes;
    int trace;                /* print every frame */
};

void adamnet_init(AdamNet *n);
/* attach a 6801: hooks its sci_tx / sci_poll; the CPU's ctx must stay the caller's,
 * so the caller forwards its callbacks to adamnet_tx / adamnet_poll. */
NetNode *adamnet_attach(AdamNet *n, M6801 *cpu, const char *name);
void adamnet_tx(NetNode *nd, uint8_t byte, uint64_t start, uint64_t end);
void adamnet_poll(NetNode *nd);
void adamnet_flush_node(NetNode *nd);   /* drop pending frames (node reset) */

#endif
