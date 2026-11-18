#ifndef Q_BIT_MASK_H
#define Q_BIT_MASK_H
#include <stdint.h>
// A shift by 32 is undefined even when the result is immediately decremented.
static inline uint32_t Q_BitMask32(int bits) {
    return bits <= 0 ? 0 : bits >= 32 ? UINT32_MAX : (UINT32_C(1) << bits) - 1;
}
#endif
