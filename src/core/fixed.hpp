#pragma once

#include <cmath>
#include <compare>
#include <cstdint>

namespace core {

constexpr uint64_t isqrt64(uint64_t n) {
    uint64_t result = 0;
    uint64_t bit = uint64_t{1} << 62;
    while (bit > n) bit >>= 2;
    while (bit != 0) {
        if (n >= result + bit) {
            n -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

// Punto fijo 16.16: la simulación no usa float para que sea determinista
// en cualquier compilador/CPU (architecture.md §9.2).
// Rango: ±32767, suficiente para coordenadas en tiles de mapas de hasta 512x512.
struct Fixed {
    static constexpr int kFracBits = 16;
    static constexpr int32_t kOne = int32_t{1} << kFracBits;

    int32_t raw = 0;

    static constexpr Fixed from_raw(int32_t r) {
        Fixed f;
        f.raw = r;
        return f;
    }
    static constexpr Fixed from_int(int32_t i) { return from_raw(i * kOne); }
    static constexpr Fixed ratio(int32_t num, int32_t den) {
        return from_raw(static_cast<int32_t>((static_cast<int64_t>(num) * kOne) / den));
    }
    // Solo para cargar datos: el resultado se redondea una vez y queda fijo.
    static Fixed from_double(double d) { return from_raw(static_cast<int32_t>(std::llround(d * kOne))); }

    constexpr int32_t floor_int() const { return raw >> kFracBits; }
    constexpr float to_float() const { return static_cast<float>(raw) / static_cast<float>(kOne); }

    constexpr Fixed operator+(Fixed o) const { return from_raw(raw + o.raw); }
    constexpr Fixed operator-(Fixed o) const { return from_raw(raw - o.raw); }
    constexpr Fixed operator-() const { return from_raw(-raw); }
    constexpr Fixed operator*(Fixed o) const {
        return from_raw(static_cast<int32_t>((static_cast<int64_t>(raw) * o.raw) >> kFracBits));
    }
    constexpr Fixed operator/(Fixed o) const {
        return from_raw(static_cast<int32_t>((static_cast<int64_t>(raw) * kOne) / o.raw));
    }
    constexpr Fixed& operator+=(Fixed o) { raw += o.raw; return *this; }
    constexpr Fixed& operator-=(Fixed o) { raw -= o.raw; return *this; }

    constexpr auto operator<=>(const Fixed&) const = default;
};

struct Vec2F {
    Fixed x;
    Fixed y;

    constexpr Vec2F operator+(Vec2F o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2F operator-(Vec2F o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2F operator*(Fixed s) const { return {x * s, y * s}; }
    constexpr bool operator==(const Vec2F&) const = default;
};

// Longitud exacta en enteros (sin overflow: los cuadrados van en 64 bits).
inline Fixed length(Vec2F v) {
    const int64_t sq = static_cast<int64_t>(v.x.raw) * v.x.raw + static_cast<int64_t>(v.y.raw) * v.y.raw;
    return Fixed::from_raw(static_cast<int32_t>(isqrt64(static_cast<uint64_t>(sq))));
}

struct TilePos {
    int32_t x = 0;
    int32_t y = 0;
    constexpr bool operator==(const TilePos&) const = default;
};

inline constexpr TilePos tile_of(Vec2F p) { return {p.x.floor_int(), p.y.floor_int()}; }
inline constexpr Vec2F tile_center(TilePos t) {
    return {Fixed::from_int(t.x) + Fixed::ratio(1, 2), Fixed::from_int(t.y) + Fixed::ratio(1, 2)};
}

}  // namespace core
