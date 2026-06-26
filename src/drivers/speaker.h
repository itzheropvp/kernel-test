/*
 * speaker.h - PC Speaker Driver
 *
 * HOW THE PC SPEAKER WORKS:
 *
 * The original IBM PC speaker is a simple piezoelectric buzzer wired to
 * two hardware components:
 *
 *   1. PIT Channel 2 (port 0x42):
 *      The same Programmable Interval Timer chip we use for the system
 *      clock, but channel 2 is dedicated to the speaker.
 *      It generates a square wave at a programmable frequency.
 *
 *   2. Port 0x61 (System Control Port B):
 *      Bit 0: Gate for PIT channel 2 (1 = enable the counter)
 *      Bit 1: Speaker output enable (1 = connect PIT output to speaker)
 *
 * To play a tone:
 *   a) Program PIT channel 2 with the desired frequency divisor
 *   b) Set bits 0 and 1 of port 0x61 to connect PIT → speaker
 *
 * To stop the tone:
 *   c) Clear bits 0 and 1 of port 0x61 (disconnect speaker)
 *
 * FREQUENCY FORMULA (same as for the system timer):
 *   divisor   = 1,193,182 / desired_hz
 *   actual_hz = 1,193,182 / divisor
 *
 * QEMU NOTE:
 *   QEMU emulates the PC speaker and routes it to your host audio.
 *   Make sure audio is enabled: qemu-system-i386 -audiodev coreaudio,...
 *
 * MUSICAL NOTE FREQUENCIES (A4 = 440 Hz standard tuning):
 *   C4=261  D4=293  E4=329  F4=349  G4=392  A4=440  B4=493
 *   C5=523  D5=587  E5=659  F5=698  G5=784  A5=880  B5=987
 */

#ifndef SPEAKER_H
#define SPEAKER_H

#include "../kernel/kernel.h"

/*
 * speaker_on - Start playing a continuous tone at the given frequency.
 *
 * @hz: frequency in Hz (20–20000 is audible range; 200–4000 sounds best)
 *
 * The speaker keeps beeping until speaker_off() is called.
 */
void speaker_on(uint32_t hz);

/*
 * speaker_off - Stop any currently playing tone.
 */
void speaker_off(void);

/*
 * speaker_beep - Play a tone for a given duration, then stop.
 *
 * @hz: frequency in Hz
 * @ms: duration in milliseconds (uses the timer, so minimum ~10 ms)
 *
 * This function BLOCKS until the beep is done.
 * Interrupts must be enabled (STI called) for the timer to work.
 */
void speaker_beep(uint32_t hz, uint32_t ms);

/*
 * speaker_note_name - Return the name of the nearest musical note.
 *
 * @hz:  frequency in Hz
 * Returns a string like "A4", "C5", "G#4", etc.
 * Used to show a visual label even when audio is silent (e.g. VirtualBox).
 */
const char* speaker_note_name(uint32_t hz);

/*
 * speaker_play_melody - Play a sequence of notes.
 *
 * @freqs:    array of frequencies (Hz); 0 = rest (silence)
 * @durations: array of durations (ms)
 * @count:    number of notes
 * @gap_ms:   silent gap between notes (ms) — prevents notes from blurring
 */
void speaker_play_melody(const uint32_t* freqs,
                         const uint32_t* durations,
                         int count,
                         uint32_t gap_ms);

#endif /* SPEAKER_H */
