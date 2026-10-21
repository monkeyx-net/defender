#include "sim/humanoids.h"

#include "sim/explosions.h"

#define SLOTS_SEARCHED 0x40     /* the ROM looks this many words on */
#define PICTURE_DEAD 0xF8D8
#define SOUND_DEAD 0xD4E4

int humanoid_remove(Game *g, uint16_t obj)
{
    uint16_t at = HUMANOIDS;
    int n = SLOTS_SEARCHED;
    while (game_word(g, at) != obj) {
        at = (uint16_t)(at + 2);
        if (!--n) return -1;
    }
    game_set_word(g, at, 0);
    if (!--g->ram[HUMANOIDS_LEFT]) processes_create(&g->processes, PLANET_GONE, 0, 0);
    return 0;
}

int humanoid_kill(Game *g, uint16_t obj)
{
    if (humanoid_remove(g, obj)) return -1;
    if (game_kill_object(g, obj)) return -1;
    Object *o = objects_at(&g->objects, obj);
    o->picture = PICTURE_DEAD;
    o->x = (uint16_t)(o->x - 0x40);
    explosion_start(g, obj);
    game_sound(g, SOUND_DEAD);
    return 0;
}

StepResult humanoid_fall(Game *g, uint16_t proc)
{
    uint16_t obj = game_word(g, (uint16_t)(proc + 7));
    Object *o = objects_at(&g->objects, obj);
    if (!o) return STEP_UNPORTED;
    uint16_t vy = (uint16_t)(o->vy + 8);
    if (vy < 0x0300) o->vy = vy;
    if (game_terrain(g, o->x) > (uint8_t)(o->y >> 8)) {
        processes_sleep(&g->processes, 4, HUMANOID_FALL);
        return STEP_DONE;
    }
    if (o->vy <= 0x00E0) return STEP_UNPORTED; /* lands safely */
    game_set_word(g, 0xA0F8, (uint16_t)(o->screen + 0x0107));
    if (humanoid_kill(g, obj)) return STEP_UNPORTED;
    game_end_process(g);
    return STEP_DONE;
}
