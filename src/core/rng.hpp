#pragma once

#include <cstdint>

namespace core {

// PCG32: única fuente de aleatoriedad de la simulación.
// Misma semilla → misma secuencia en cualquier plataforma (a diferencia de <random>).
class Rng {
public:
    explicit Rng(uint64_t seed = 0x853c49e6748fea9bULL, uint64_t stream = 0xda3e39cb94b95bdbULL) {
        inc_ = (stream << 1u) | 1u;
        next();
        state_ += seed;
        next();
    }

    uint32_t next() {
        const uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const auto xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    // [0, n)
    uint32_t below(uint32_t n) { return n == 0 ? 0 : next() % n; }

    // [lo, hi] (inclusive)
    int32_t range(int32_t lo, int32_t hi) {
        if (hi <= lo) return lo;
        return lo + static_cast<int32_t>(below(static_cast<uint32_t>(hi - lo + 1)));
    }

private:
    uint64_t state_ = 0;
    uint64_t inc_ = 0;
};

}  // namespace core
