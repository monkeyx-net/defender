#include "sim/sounds.h"

#define IN0_THRUST 0x02
#define NO_THRUST_SOUND 0x98 /* <$BA bits that keep the thrust sound off */

void sounds_start(Sounds *s, const uint8_t *script)
{
    s->thrust = 0;
    if (script[0] < s->priority) return;
    s->priority = script[0];
    s->script = script;
    s->pos = -2; /* the step below moves to the first entry */
    s->timer = 1;
    s->repeat = 1;
}

int sounds_tick(Sounds *s, uint8_t in0, uint8_t state)
{
    if (s->timer) {
        if (--s->timer) return -1;
        if (--s->repeat) {
            s->timer = s->script[s->pos + 1];
            return s->script[s->pos + 2];
        }
        s->pos += 3;
        uint8_t count = s->script[s->pos];
        if (count) {
            s->repeat = count;
            s->timer = s->script[s->pos + 1];
            return s->script[s->pos + 2];
        }
        s->priority = 0;
    }
    if (in0 & IN0_THRUST) {
        if (s->thrust || (state & NO_THRUST_SOUND)) return -1;
        s->thrust = SOUND_THRUST_ON;
        return SOUND_THRUST_ON;
    }
    if (!s->thrust) return -1;
    s->thrust = 0;
    return SOUND_THRUST_OFF;
}
