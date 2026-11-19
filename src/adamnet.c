/* adamnet.c - AdamNet bus. Times on the wire are µs; a node whose 6801 runs at
 * E = 1 MHz << clock_shift converts its E cycles here. */
#include "adamnet.h"
#include <stdio.h>
#include <string.h>

void adamnet_init(AdamNet *n) { memset(n, 0, sizeof *n); }

NetNode *adamnet_attach(AdamNet *n, M6801 *cpu, const char *name)
{
    NetNode *nd = &n->node[n->nnodes];
    nd->net = n; nd->cpu = cpu; nd->cursor = n->count; nd->name = name; nd->index = n->nnodes;
    n->nnodes++;
    cpu->rx_due = ~0ull;
    return nd;
}

static void update_due(NetNode *nd)
{
    AdamNet *n = nd->net;
    nd->cpu->rx_due = nd->cursor < n->count ? n->frame[nd->cursor % NET_FRAMES].end << nd->cpu->clock_shift : ~0ull;
}

void adamnet_tx(NetNode *nd, uint8_t byte, uint64_t start, uint64_t end)
{
    AdamNet *n = nd->net;
    start >>= nd->cpu->clock_shift; end >>= nd->cpu->clock_shift;
    /* frames are appended in start order; a node that runs earlier in the time slice
     * may report a slightly earlier frame later: insert it in place */
    unsigned i = n->count;
    n->count++;
    while (i > 0 && i + NET_FRAMES > n->count && n->frame[(i - 1) % NET_FRAMES].start > start) {
        n->frame[i % NET_FRAMES] = n->frame[(i - 1) % NET_FRAMES];
        i--;
    }
    NetFrame *f = &n->frame[i % NET_FRAMES];
    f->start = start; f->end = end; f->byte = byte; f->src = (uint8_t)nd->index;
    if (n->trace) fprintf(stderr, "[net %10llu] %-6s -> %02X\n", (unsigned long long)start, nd->name, byte);
    for (int k = 0; k < n->nnodes; k++) {
        NetNode *o = &n->node[k];
        if (o->cursor > i) o->cursor = i;      /* re-deliver from the inserted frame */
        update_due(o);
    }
}

void adamnet_poll(NetNode *nd)
{
    AdamNet *n = nd->net;
    if (n->count - nd->cursor > NET_FRAMES) nd->cursor = n->count - NET_FRAMES;
    while (nd->cursor < n->count) {
        NetFrame *f = &n->frame[nd->cursor % NET_FRAMES];
        int sh = nd->cpu->clock_shift;
        if (f->end > nd->cpu->cycles >> sh) break;
        uint64_t idle_since = 0;
        if (nd->cursor > 0 && n->count - nd->cursor < NET_FRAMES)
            idle_since = n->frame[(nd->cursor - 1) % NET_FRAMES].end;
        m6801_sci_receive(nd->cpu, f->byte, f->start << sh, idle_since << sh);
        nd->cursor++;
    }
    update_due(nd);
}

void adamnet_flush_node(NetNode *nd)
{
    nd->cursor = nd->net->count;
    update_due(nd);
}
