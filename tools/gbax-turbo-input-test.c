/* SPDX-License-Identifier: MPL-2.0 */
#include "gbax-turbo-input.h"
#include <assert.h>
int main(void) {
    const unsigned halves[] = {1,2,3,6};
    for (unsigned h = 0; h < 4; ++h) {
        unsigned half = halves[h];
        struct GBAXTurboInput a = {0}, b = {0};
        for (unsigned frame = 0; frame < 120; ++frame) {
            assert(GBAXTurboPulse(&a, true, half) == (frame % (2*half) < half));
            assert(!GBAXTurboPulse(&b, false, half));
        }
        assert(!GBAXTurboPulse(&a, false, half));
        assert(GBAXTurboPulse(&a, true, half));
        assert(GBAXTurboPulse(&b, true, half));
        for (unsigned frame = 1; frame < half; ++frame) assert(GBAXTurboPulse(&b, true, half));
        assert(!GBAXTurboPulse(&b, true, half));
        assert(!GBAXTurboPulse(&b, false, half));
        assert(GBAXTurboPulse(&b, true, half));
    }
}
