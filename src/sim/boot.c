#include "sim/boot.h"

#include <string.h>

#include "sim/attract.h"
#include "sim/cmos.h"
#include "sim/turn.h"

void game_power_on(Game *g)
{
    static const uint8_t zero[GAME_RAM];
    game_load(g, zero);
    uint8_t *r = g->ram;
    uint8_t credits = cmos_read(g, 0xC47D);
    if (credits > 0x20 || (credits & 0x0F) > 9) credits = 0;
    r[0xA037] = credits;
    game_set_word(g, 0xA0E0, 0xA55A);
    game_set_word(g, 0xA0A1, 0xFF70);
    r[0xA0A3] = 0;
    game_set_word(g, 0xA079, 0xFFFF);
    game_set_word(g, 0xA090, 0xDF17); /* $D8DC: JMP $DF17 at <$8F */
    r[0xA08F] = 0x7E;
    r[0xA036] = 3;
    for (int i = 0; i < 48; i++) { /* bank 3 $CCC1 */
        uint8_t v = game_read_bank(g, 3, (uint16_t)(0xCECF + i));
        uint16_t at = (uint16_t)(0xB260 + 2 * i);
        game_poke(g, (uint16_t)(at + 1), v);
        game_poke(g, at, (uint8_t)(v >> 4));
    }
    processes_init(&g->processes);
    game_reset(g);
    r[0xA052] = 0; /* $F800 */
    r[0xA049] = 0;
    r[0xA036] = 3;
    r[0xA048] = 3;
    game_set_word(g, 0xA059, 0xFFFF);
    processes_create(&g->processes, ATTRACT, 1, 0);
    r[0xA0BA] = (uint8_t)~r[0xA0BA];
    g->sound_sent = -1;
    g->split = -1; /* the C game's own timing */
    g->bottom_at_write = -1;
}
