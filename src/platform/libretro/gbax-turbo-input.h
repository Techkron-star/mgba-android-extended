/* SPDX-License-Identifier: MPL-2.0 */
#ifndef GBAX_TURBO_INPUT_H
#define GBAX_TURBO_INPUT_H
#include <stdbool.h>
struct GBAXTurboInput { unsigned phase; };
/* Start on a pressed frame; release resets phase. One call per emulated frame.
 * halfPeriod is validated by the frontend (1, 2, 3 or 6). */
static inline bool GBAXTurboPulse(struct GBAXTurboInput* input, bool held, unsigned halfPeriod) {
    if (!held || !halfPeriod) {
        input->phase = 0;
        return false;
    }
    if (input->phase >= 2 * halfPeriod) input->phase = 0;
    bool pressed = input->phase < halfPeriod;
    ++input->phase;
    return pressed;
}
#endif
