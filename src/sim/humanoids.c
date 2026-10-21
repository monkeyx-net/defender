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
