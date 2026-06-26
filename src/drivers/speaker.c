/*
 * speaker.c - PC Speaker Driver Implementation
 */

#include "speaker.h"
#include "../kernel/timer.h"

/* PIT I/O ports */
#define PIT_CHANNEL2  0x42   /* Channel 2 data port (speaker frequency) */
#define PIT_CMD       0x43   /* Mode/Command register                    */

/*
 * PIT command byte for channel 2: 0xB6
 *   Bits 7-6: 10   = Channel 2
 *   Bits 5-4: 11   = Access mode: lobyte then hibyte
 *   Bits 3-1: 011  = Mode 3: Square Wave Generator
 *   Bit  0:   0    = Binary (not BCD)
 *   0xB6 = 1011 0110
 */
#define PIT_CH2_SQUARE_WAVE  0xB6

/* Port 0x61 — System Control Port B */
#define SPEAKER_CTRL_PORT    0x61
#define SPEAKER_ENABLE_BITS  0x03  /* Bit 0: PIT gate, Bit 1: speaker out */

/* PIT base clock (same constant as in timer.c) */
#define PIT_BASE_FREQ  1193182

void speaker_on(uint32_t hz) {
    if (hz == 0) return;

    /*
     * Step 1: Program PIT channel 2 with the frequency divisor.
     *
     * The PIT counts down from 'divisor' and generates one output
     * pulse per countdown — at the rate of PIT_BASE_FREQ / divisor Hz.
     */
    uint32_t divisor = PIT_BASE_FREQ / hz;

    outb(PIT_CMD, PIT_CH2_SQUARE_WAVE);          /* Set channel 2 mode */
    outb(PIT_CHANNEL2, (uint8_t)(divisor & 0xFF));        /* Low byte  */
    outb(PIT_CHANNEL2, (uint8_t)((divisor >> 8) & 0xFF)); /* High byte */

    /*
     * Step 2: Connect PIT channel 2 output to the speaker.
     *
     * Port 0x61 controls several system functions.
     * We only touch bits 0-1 (speaker enable), preserving the rest.
     *   Bit 0 = 1: enable PIT channel 2 gate (let counter run)
     *   Bit 1 = 1: enable speaker output (connect PIT to buzzer)
     */
    uint8_t ctrl = inb(SPEAKER_CTRL_PORT);
    outb(SPEAKER_CTRL_PORT, ctrl | SPEAKER_ENABLE_BITS);
}

void speaker_off(void) {
    /*
     * Clear bits 0 and 1 of port 0x61 to disconnect the speaker.
     * We use AND with ~0x03 (= 0xFC) to clear only those two bits.
     */
    uint8_t ctrl = inb(SPEAKER_CTRL_PORT);
    outb(SPEAKER_CTRL_PORT, ctrl & ~SPEAKER_ENABLE_BITS);
}

void speaker_beep(uint32_t hz, uint32_t ms) {
    /* Convert milliseconds to timer ticks.
     * Our timer runs at 100 Hz → 1 tick = 10 ms.
     * Round up so even very short beeps produce at least 1 tick.
     */
    uint32_t ticks = (ms + 9) / 10;
    if (ticks == 0) ticks = 1;

    speaker_on(hz);
    timer_sleep(ticks);
    speaker_off();
}

/* ============================================================
 * NOTE NAME LOOKUP TABLE
 *
 * Maps frequencies (Hz) to musical note names.
 * Standard western scale, A4 = 440 Hz tuning.
 *
 * To find the nearest note we do a simple linear scan:
 * compare the distance (|freq - table[i].hz|) for each entry
 * and return the one with the smallest distance.
 *
 * We can't use log2() here (no floating-point math in the kernel),
 * so the lookup table is the simplest correct approach.
 * ============================================================ */
typedef struct { const char* name; uint32_t hz; } note_entry_t;

static const note_entry_t note_table[] = {
    {"C3",  130}, {"C#3", 138}, {"D3",  146}, {"D#3", 155}, {"E3",  164},
    {"F3",  174}, {"F#3", 185}, {"G3",  196}, {"G#3", 207}, {"A3",  220},
    {"A#3", 233}, {"B3",  246},
    {"C4",  261}, {"C#4", 277}, {"D4",  293}, {"D#4", 311}, {"E4",  329},
    {"F4",  349}, {"F#4", 369}, {"G4",  392}, {"G#4", 415}, {"A4",  440},
    {"A#4", 466}, {"B4",  493},
    {"C5",  523}, {"C#5", 554}, {"D5",  587}, {"D#5", 622}, {"E5",  659},
    {"F5",  698}, {"F#5", 739}, {"G5",  784}, {"G#5", 830}, {"A5",  880},
    {"A#5", 932}, {"B5",  987},
    {"C6", 1046}, {"C#6",1108}, {"D6", 1174}, {"D#6",1244}, {"E6", 1318},
    {"F6", 1396}, {"F#6",1479}, {"G6", 1567}, {"G#6",1661}, {"A6", 1760},
};
#define NOTE_TABLE_SIZE ((int)(sizeof(note_table) / sizeof(note_table[0])))

const char* speaker_note_name(uint32_t hz) {
    if (hz == 0) return "rest";

    const char* best = note_table[0].name;
    uint32_t    best_dist = (hz > note_table[0].hz)
                          ? hz - note_table[0].hz
                          : note_table[0].hz - hz;

    for (int i = 1; i < NOTE_TABLE_SIZE; i++) {
        uint32_t dist = (hz > note_table[i].hz)
                      ? hz - note_table[i].hz
                      : note_table[i].hz - hz;
        if (dist < best_dist) {
            best_dist = dist;
            best      = note_table[i].name;
        }
    }
    return best;
}

void speaker_play_melody(const uint32_t* freqs,
                         const uint32_t* durations,
                         int count,
                         uint32_t gap_ms) {
    for (int i = 0; i < count; i++) {
        if (freqs[i] == 0) {
            /* Rest: silence for the duration */
            uint32_t ticks = (durations[i] + 9) / 10;
            timer_sleep(ticks ? ticks : 1);
        } else {
            speaker_beep(freqs[i], durations[i]);
        }

        /* Brief silence between notes — without this, two adjacent notes
         * of the same pitch blur into one long tone */
        if (gap_ms > 0 && i < count - 1) {
            uint32_t gap_ticks = (gap_ms + 9) / 10;
            timer_sleep(gap_ticks ? gap_ticks : 1);
        }
    }
}
