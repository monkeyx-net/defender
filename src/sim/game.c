#include "sim/game.h"

#include "sim/background.h"
#include "sim/baiter.h"
#include "sim/bombers.h"
#include "sim/humanoids.h"
#include "sim/lander.h"
#include "sim/laser.h"
#include "sim/pods.h"
#include "sim/wave.h"
#include "sim/rng.h"
#include "sim/sounds.h"

#define RNG 0xA0DF        /* <$DF-<$E1 */
#define SOUND_SCRIPT 0xA0B0
#define SOUND_PRIORITY 0xA0B2
#define SOUND_THRUST 0xA0AD

uint8_t game_read(const Game *g, uint16_t addr)
{
    return addr < GAME_RAM ? (uint8_t)game_peek(g, addr) : g->data[addr];
}

uint8_t game_read_bank(const Game *g, int bank, uint16_t addr)
{
    if (addr >= 0xC000 && addr < 0xD000) return g->bank[bank] ? g->bank[bank][addr - 0xC000] : 0;
    return game_read(g, addr);
}

int game_peek(const Game *g, uint16_t addr)
{
    int v = objects_peek(&g->objects, addr);
    if (v >= 0) return v;
    v = processes_peek(&g->processes, addr);
    if (v >= 0) return v;
    return addr < GAME_RAM ? g->ram[addr] : -1;
}

void game_poke(Game *g, uint16_t addr, uint8_t value)
{
    if (g->on_write) g->on_write(g->user, addr);
    if (objects_peek(&g->objects, addr) >= 0) objects_poke(&g->objects, addr, value);
    else if (processes_peek(&g->processes, addr) >= 0) processes_poke(&g->processes, addr, value);
    else if (addr < GAME_RAM) g->ram[addr] = value;
}

uint16_t game_word(const Game *g, uint16_t addr)
{
    return (uint16_t)((game_peek(g, addr) & 0xFF) << 8 | (game_peek(g, (uint16_t)(addr + 1)) & 0xFF));
}

void game_set_word(Game *g, uint16_t addr, uint16_t value)
{
    game_poke(g, addr, (uint8_t)(value >> 8));
    game_poke(g, (uint16_t)(addr + 1), (uint8_t)value);
}

uint8_t game_random(Game *g)
{
    Rng r = { g->ram[RNG], g->ram[RNG + 1], g->ram[RNG + 2] };
    uint8_t v = rng_step(&r);
    g->ram[RNG] = r.value;
    g->ram[RNG + 1] = r.hi;
    g->ram[RNG + 2] = r.lo;
    return v;
}

uint8_t game_random_upto(Game *g, uint8_t n)
{
    uint8_t r = game_random(g);
    while (r > n) r >>= 1;
    return (uint8_t)(r + 1);
}

void game_sound(Game *g, uint16_t script)
{
    Sounds s = {
        .script = g->data, /* <$B0 is an address: script + pos */
        .pos = game_word(g, SOUND_SCRIPT),
        .priority = g->ram[SOUND_PRIORITY],
        .timer = g->ram[SOUND_PRIORITY + 1],
        .repeat = g->ram[SOUND_PRIORITY + 2],
        .thrust = g->ram[SOUND_THRUST],
    };
    sounds_start(&s, g->data + script);
    game_set_word(g, SOUND_SCRIPT, (uint16_t)((s.script - g->data) + s.pos));
    g->ram[SOUND_PRIORITY] = s.priority;
    g->ram[SOUND_PRIORITY + 1] = s.timer;
    g->ram[SOUND_PRIORITY + 2] = s.repeat;
    g->ram[SOUND_THRUST] = s.thrust;
}

#define BANK 0xA036 /* <$36: the ROM bank the code last selected */

uint8_t game_terrain(const Game *g, uint16_t x) { return (uint8_t)game_peek(g, (uint16_t)(0xB300 + (x >> 6))); }

void game_send_sound(Game *g, uint8_t sound) { g->sound_sent = sound; }

void game_erase_object(Game *g, uint16_t obj)
{
    g->ram[BANK] = 2; /* $F503 */
    uint16_t at = game_word(g, obj + 4), picture = game_word(g, obj + 2);
    int w = game_read(g, picture), h = game_read(g, (uint16_t)(picture + 1));
    for (int c = 0; c < w; c++)
        for (int r = 0; r < h; r++) game_poke(g, (uint16_t)(at + c * 0x100 + r), 0);
}

int game_kill_object(Game *g, uint16_t obj)
{
    if (objects_free(&g->objects, obj)) return -1;
    game_erase_object(g, obj);
    return 0;
}

void game_end_process(Game *g) { processes_kill(&g->processes, g->processes.current); }

int game_step_ported(const Game *g, uint16_t proc)
{
    const Process *p = processes_at((Processes *)&g->processes, proc); /* only read */
    if (!p) return 0;
    switch (p->resume) {
    case LANDER_SEEK:
    case LANDER_ALIGN:
    case LANDER_RISE:
    case LANDER_TOP:
    case MUTANT:
    case HUMANOID_FALL:
    case HUMANOID_CARRIED_BY_SHIP:
    case HUMANOID_WALK:
    case BONUS_250:
    case BONUS_500:
    case BONUS_SHOW:
    case LASER_FIRE:
    case LASER_TICK_RIGHT:
    case LASER_TICK_LEFT:
    case BAITER:
    case SWARMER_AIM:
    case SWARMER:
    case BOMBERS:
    case WAVE:
    case COLOUR_CYCLE:
    case COLOUR_CYCLE_NEXT:
    case POINTERS:
    case PALETTE_CYCLE:
    case PALETTE_CYCLE_NEXT:
    case FLASH:
    case FLASH_NEXT:
    case SCORE_BLINK:
    case SCORE_BLINK_DRAW:
    case HOUSEKEEPING:
    case HOUSEKEEPING_2:
    case HOUSEKEEPING_3: return 1;
    }
    return 0;
}

StepResult game_step(Game *g, uint16_t proc)
{
    Process *p = processes_at(&g->processes, proc);
    if (!p) return STEP_UNPORTED;
    switch (p->resume) {
    case LANDER_SEEK: return lander_seek(g, proc);
    case LANDER_ALIGN: return lander_align(g, proc);
    case LANDER_RISE: return lander_rise(g, proc);
    case LANDER_TOP: return lander_top(g, proc);
    case MUTANT: return mutant_step(g, proc);
    case HUMANOID_FALL: return humanoid_fall(g, proc);
    case HUMANOID_CARRIED_BY_SHIP: return humanoid_carried(g, proc);
    case HUMANOID_WALK: return humanoid_walk(g, proc);
    case BONUS_250: return bonus_start(g, proc, 0);
    case BONUS_500: return bonus_start(g, proc, 1);
    case BONUS_SHOW: return bonus_end(g, proc);
    case LASER_FIRE: return laser_fire(g, proc);
    case BAITER: return baiter_step(g, proc);
    case SWARMER_AIM: return swarmer_step(g, proc, 1);
    case SWARMER: return swarmer_step(g, proc, 0);
    case BOMBERS: return bomber_step(g, proc);
    case COLOUR_CYCLE:
    case COLOUR_CYCLE_NEXT:
    case POINTERS:
    case PALETTE_CYCLE:
    case PALETTE_CYCLE_NEXT:
    case FLASH:
    case FLASH_NEXT:
    case SCORE_BLINK:
    case SCORE_BLINK_DRAW:
    case HOUSEKEEPING:
    case HOUSEKEEPING_2:
    case HOUSEKEEPING_3: return background_step(g, proc, p->resume);
    case WAVE: return wave_step(g, proc);
    case LASER_TICK_RIGHT:
    case LASER_TICK_LEFT: return laser_step(g, proc, p->resume == LASER_TICK_LEFT);
    }
    return STEP_UNPORTED;
}
