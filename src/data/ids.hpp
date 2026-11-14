#pragma once

#include <array>
#include <bitset>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace data {

// Índice de una ficha (unidad, edificio, recurso) en DataRegistry.
// El motor nunca nombra tipos concretos: los obtiene de data/*.json.
struct TypeId {
    uint16_t value = std::numeric_limits<uint16_t>::max();
    constexpr bool valid() const { return value != std::numeric_limits<uint16_t>::max(); }
    constexpr auto operator<=>(const TypeId&) const = default;
};

using ClassId = uint8_t;
inline constexpr std::size_t kMaxClasses = 64;

// Una entidad pertenece a varias clases a la vez ("unit", "cavalry", "military"...).
struct ClassSet {
    std::bitset<kMaxClasses> bits;
    bool has(ClassId c) const { return bits.test(c); }
    void add(ClassId c) { bits.set(c); }
    bool intersects(const ClassSet& o) const { return (bits & o.bits).any(); }
    bool operator==(const ClassSet&) const = default;
};

// 0 = Gaia (naturaleza: árboles, minas). Jugadores: 1..8.
using PlayerId = uint8_t;
inline constexpr PlayerId kGaia = 0;
inline constexpr int kMaxPlayers = 9;

enum class ResourceTag : uint8_t { Food, Wood, Gold, Stone };
inline constexpr std::size_t kResourceCount = 4;
using ResourceArray = std::array<int32_t, kResourceCount>;

inline constexpr std::size_t index_of(ResourceTag r) { return static_cast<std::size_t>(r); }

}  // namespace data
