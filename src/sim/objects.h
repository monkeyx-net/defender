/*
 * The game's objects: a port of the object pool and lists. 95 blocks of
 * 23 bytes at $A23C; the C structs keep the ROM's layout and values (a
 * block is named by its RAM address, pictures and handlers by their ROM
 * addresses), so they can be compared with the board's RAM byte for byte.
 *
 * Lists (heads in RAM): $A065 the objects on screen (drawn, moved, hit
 * tested), $A06B those off screen, $A06D shots and bombs, $A067 the free
 * blocks. $D0AD takes a free block and links it in front of the screen
 * list's head without making it the head ($FC69, the appearing effect,
 * and others do that); $D095 also sets owner, picture, hit handler and a
 * data word; $D0C7 frees a block from the screen or off-screen list,
 * $D0F2 from the shots; $E376 (the interrupt) moves the screen list.
 */
#ifndef DEFENDER_OBJECTS_H
#define DEFENDER_OBJECTS_H

#include <stdint.h>

#define OBJECTS_FIRST 0xA23C
#define OBJECT_SIZE 23
#define OBJECTS 95
#define LIST_SCREEN 0xA065
#define LIST_FREE 0xA067
#define LIST_OFF_SCREEN 0xA06B
#define LIST_SHOTS 0xA06D

typedef struct {
    uint16_t link;      /* +0 the next block, 0 at the end */
    uint16_t picture;   /* +2 picture descriptor */
    uint16_t screen;    /* +4 where it was drawn ((column << 8) | line), 0 if not */
    uint16_t owner;     /* +6 its process */
    uint16_t hit;       /* +8 hit handler */
    uint16_t x;         /* +A world x */
    uint16_t y;         /* +C 8.8, lines $2A-$F0 */
    uint16_t vx, vy;    /* +E, +10 */
    uint16_t data;      /* +12 */
    uint8_t flags;      /* +14 */
    uint8_t extra[2];   /* +15, +16 */
} Object;

typedef struct {
    Object block[OBJECTS];
    uint16_t screen, free_list, off_screen, shots; /* the list heads */
} Objects;

/* The block at a RAM address, or NULL. */
Object *objects_at(Objects *o, uint16_t addr);

/* $D805: every block free, in address order; the lists empty. Only the
   links change (zero the struct once at power-on). */
void objects_init(Objects *o);
/* $D0AD: a free block, linked ahead of the screen list's head; 0 if none
   (the ROM stops with an error). */
uint16_t objects_alloc(Objects *o);
/* $D095 after $D0AD: the new block's owner, picture, hit handler, data. */
void objects_setup(Objects *o, uint16_t addr, uint16_t owner, uint16_t picture, uint16_t hit, uint16_t data);
/* $D0C7: free a block on the screen or off-screen list; $D0F2: on the
   shots list. 0, or -1 if it is on neither (the ROM stops with an error). */
int objects_free(Objects *o, uint16_t addr);
int objects_free_shot(Objects *o, uint16_t addr);
/* $E376: move every object on screen (not while `state`, <$BA, has bit 5). */
void objects_move(Objects *o, uint8_t state);

/* The RAM view: one byte at an address in the blocks or the list heads
   (-1 elsewhere), and a write there. */
/* The blocks and heads from a RAM image (indexed by address). */
void objects_load(Objects *o, const uint8_t *ram);
int objects_peek(const Objects *o, uint16_t addr);
void objects_poke(Objects *o, uint16_t addr, uint8_t value);

#endif
