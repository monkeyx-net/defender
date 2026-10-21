#include "sim/enemies.h"

#include "sim/baiter.h"
#include "sim/bombers.h"
#include "sim/explosions.h"
#include "sim/humanoids.h"
#include "sim/pods.h"
#include "sim/score.h"

#define LANDERS 0xA112
#define MUTANTS 0xA115
#define POINTS_150 0x0115
#define SOUND_LANDER 0xD507
#define SOUND_MUTANT 0xD4F8
#define SOUND_DROPPED 0xD4E9

int enemy_kill(Game *g, uint16_t obj, uint16_t points, uint16_t script, int free)
{
    if (!processes_kill(&g->processes, game_word(g, (uint16_t)(obj + 6)))) return -1;
    if (free && objects_free(&g->objects, obj)) return -1;
    if (score_add(g, points)) return -1;
    game_erase_object(g, obj);
    explosion_start(g, obj);
    game_sound(g, script);
    return 0;
}

static int lander_hit(Game *g, uint16_t obj)
{
    g->ram[LANDERS]--;
    return enemy_kill(g, obj, POINTS_150, SOUND_LANDER, 1);
}

static int lander_carrying_hit(Game *g, uint16_t obj)
{
    uint16_t proc = game_word(g, (uint16_t)(obj + 6));
    if (game_word(g, game_word(g, (uint16_t)(proc + 0xB)))) {
        uint16_t fall = processes_create(&g->processes, HUMANOID_FALL, 0, 0);
        if (!fall) return -1;
        uint16_t human = game_word(g, (uint16_t)(proc + 9));
        game_set_word(g, (uint16_t)(fall + 7), human);
        game_sound(g, SOUND_DROPPED);
        game_set_word(g, (uint16_t)(human + 0x10), 0);
        game_set_word(g, (uint16_t)(human + 6), fall);
    }
    return lander_hit(g, obj);
}

static int done(int r) { return r ? -1 : 1; }

int enemy_hit(Game *g, uint16_t handler, uint16_t obj)
{
    switch (handler) {
    case HIT_LANDER: return done(lander_hit(g, obj));
    case HIT_LANDER_CARRYING: return done(lander_carrying_hit(g, obj));
    case HIT_MUTANT:
        g->ram[MUTANTS]--;
        return done(enemy_kill(g, obj, POINTS_150, SOUND_MUTANT, 1));
    case HIT_BAITER: return done(baiter_hit(g, obj));
    case HIT_POD: return done(pod_hit(g, obj));
    case HIT_SWARMER: return done(swarmer_hit(g, obj));
    case HIT_BOMBER: return done(bomber_hit(g, obj));
    case HUMANOID_ON_GROUND: return humanoid_hit(g, obj, 0);
    case HUMANOID_CARRIED: return humanoid_hit(g, obj, 1);
    }
    return -1;
}
