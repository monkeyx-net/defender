#include "sim/processes.h"

#include <stddef.h>

static int index_of(uint16_t addr, uint16_t first, int size, int count)
{
    if (addr < first) return -1;
    unsigned off = (unsigned)(addr - first);
    if (off % (unsigned)size || off / (unsigned)size >= (unsigned)count) return -1;
    return (int)(off / (unsigned)size);
}

Process *processes_at(Processes *p, uint16_t addr)
{
    int i = index_of(addr, PROCESSES_FIRST, PROCESS_SIZE, PROCESSES);
    if (i >= 0) return &p->small[i];
    i = index_of(addr, LARGE_FIRST, LARGE_SIZE, LARGES);
    return i >= 0 ? &p->large[i] : NULL;
}

/* The link field at `addr`: a block's +0, or the list head's pseudo block. */
static uint16_t *link_at(Processes *p, uint16_t addr)
{
    if (addr == PROCESS_HEAD) return &p->head;
    Process *b = processes_at(p, addr);
    return b ? &b->link : NULL;
}

void processes_init(Processes *p)
{
    for (int i = 0; i < PROCESSES; i++)
        p->small[i].link = i + 1 < PROCESSES ? (uint16_t)(PROCESSES_FIRST + (i + 1) * PROCESS_SIZE) : 0;
    for (int i = 0; i < LARGES; i++)
        p->large[i].link = i + 1 < LARGES ? (uint16_t)(LARGE_FIRST + (i + 1) * LARGE_SIZE) : 0;
    p->free_small = PROCESSES_FIRST;
    p->free_large = LARGE_FIRST;
    p->head = 0;
    p->current = PROCESS_HEAD;
}

uint16_t processes_create(Processes *p, uint16_t resume, uint8_t type, int large)
{
    uint16_t *free_list = large ? &p->free_large : &p->free_small;
    uint16_t addr = *free_list;
    Process *b = processes_at(p, addr);
    uint16_t *after = link_at(p, p->current);
    if (!b || !after) return 0;
    *free_list = b->link;
    b->large = large ? 1 : 0;
    b->resume = resume;
    b->type = type;
    b->sleep = 1;
    b->link = *after;
    *after = addr;
    return addr;
}

void processes_sleep(Processes *p, uint8_t ticks, uint16_t resume)
{
    Process *b = processes_at(p, p->current);
    if (!b) return;
    b->sleep = ticks;
    b->resume = resume;
}

uint16_t processes_kill(Processes *p, uint16_t addr)
{
    uint16_t prev = PROCESS_HEAD;
    for (;;) {
        uint16_t *link = link_at(p, prev);
        if (!link || !*link) return 0;
        if (*link == addr) {
            Process *b = processes_at(p, addr);
            if (!b) return 0;
            *link = b->link;
            uint16_t *free_list = b->large ? &p->free_large : &p->free_small;
            b->link = *free_list;
            *free_list = addr;
            return prev;
        }
        prev = *link;
    }
}

int processes_tick(Processes *p, uint16_t addr)
{
    Process *b = processes_at(p, addr);
    if (!b || --b->sleep) return 0;
    p->current = addr;
    return 1;
}

/* RAM view, as objects.c: the heads, then the blocks' bytes. */
static uint16_t *word_at(Processes *p, uint16_t addr, int *low)
{
    static const uint16_t at[4] = { PROCESS_HEAD, PROCESS_FREE, PROCESS_CURRENT, PROCESS_FREE_LARGE };
    uint16_t *w[4] = { &p->head, &p->free_small, &p->current, &p->free_large };
    for (int i = 0; i < 4; i++)
        if (addr == at[i] || addr == at[i] + 1) {
            *low = addr != at[i];
            return w[i];
        }
    return NULL;
}

/* The block holding `addr` and the offset in it. */
static Process *block_of(Processes *p, uint16_t addr, unsigned *off)
{
    if (addr >= PROCESSES_FIRST && addr < PROCESSES_FIRST + PROCESSES * PROCESS_SIZE) {
        *off = (unsigned)(addr - PROCESSES_FIRST) % PROCESS_SIZE;
        return &p->small[(addr - PROCESSES_FIRST) / PROCESS_SIZE];
    }
    if (addr >= LARGE_FIRST && addr < LARGE_FIRST + LARGES * LARGE_SIZE) {
        *off = (unsigned)(addr - LARGE_FIRST) % LARGE_SIZE;
        return &p->large[(addr - LARGE_FIRST) / LARGE_SIZE];
    }
    return NULL;
}

static uint8_t *byte_of(Process *b, unsigned off, uint8_t *word_bytes)
{
    word_bytes[0] = (uint8_t)(b->link >> 8);
    word_bytes[1] = (uint8_t)b->link;
    word_bytes[2] = (uint8_t)(b->resume >> 8);
    word_bytes[3] = (uint8_t)b->resume;
    if (off < 4) return &word_bytes[off];
    if (off == 4) return &b->sleep;
    if (off == 5) return &b->type;
    if (off == 6) return &b->large;
    return &b->data[off - 7];
}

int processes_peek(const Processes *p, uint16_t addr)
{
    Processes *m = (Processes *)p; /* only read */
    int low;
    uint16_t *w = word_at(m, addr, &low);
    if (w) return low ? *w & 0xFF : *w >> 8;
    unsigned off;
    Process *b = block_of(m, addr, &off);
    if (!b) return -1;
    uint8_t words[4];
    return *byte_of(b, off, words);
}

void processes_poke(Processes *p, uint16_t addr, uint8_t value)
{
    int low;
    uint16_t *w = word_at(p, addr, &low);
    if (w) {
        *w = low ? (uint16_t)((*w & 0xFF00) | value) : (uint16_t)((*w & 0xFF) | value << 8);
        return;
    }
    unsigned off;
    Process *b = block_of(p, addr, &off);
    if (!b) return;
    uint8_t words[4];
    *byte_of(b, off, words) = value;
    if (off < 4) {
        b->link = (uint16_t)(words[0] << 8 | words[1]);
        b->resume = (uint16_t)(words[2] << 8 | words[3]);
    }
}
