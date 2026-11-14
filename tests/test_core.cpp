#include <doctest.h>

#include "core/fixed.hpp"
#include "core/rng.hpp"
#include "core/slot_map.hpp"

using core::Fixed;

TEST_CASE("SlotMap: un handle muerto no devuelve otra entidad") {
    core::SlotMap<int> map;
    const auto a = map.insert(1);
    const auto b = map.insert(2);
    CHECK(*map.get(a) == 1);
    CHECK(*map.get(b) == 2);

    CHECK(map.erase(a));
    CHECK(map.get(a) == nullptr);
    CHECK_FALSE(map.erase(a));

    const auto c = map.insert(3);  // reutiliza el hueco de a
    CHECK(c.index == a.index);
    CHECK(c.generation != a.generation);
    CHECK(map.get(a) == nullptr);
    CHECK(*map.get(c) == 3);
    CHECK(map.size() == 2);
}

TEST_CASE("SlotMap: for_each recorre en orden de índice") {
    core::SlotMap<int> map;
    for (int i = 0; i < 5; ++i) map.insert(i);
    map.erase({1, 0});
    std::vector<int> seen;
    map.for_each([&](core::EntityHandle, int v) { seen.push_back(v); });
    CHECK(seen == std::vector<int>{0, 2, 3, 4});
}

TEST_CASE("Fixed: aritmética básica") {
    CHECK(Fixed::from_int(3) + Fixed::from_int(4) == Fixed::from_int(7));
    CHECK(Fixed::from_int(3) * Fixed::ratio(1, 2) == Fixed::ratio(3, 2));
    CHECK(Fixed::from_int(7) / Fixed::from_int(2) == Fixed::ratio(7, 2));
    CHECK(Fixed::ratio(-3, 2).floor_int() == -2);
    CHECK(Fixed::from_double(0.25) == Fixed::ratio(1, 4));
    CHECK(core::length({Fixed::from_int(3), Fixed::from_int(4)}) == Fixed::from_int(5));
    CHECK(core::length({Fixed::from_int(200), Fixed::from_int(200)}).floor_int() == 282);  // sin overflow
}

TEST_CASE("Rng: misma semilla, misma secuencia") {
    core::Rng a(42), b(42), c(43);
    bool differs = false;
    for (int i = 0; i < 100; ++i) {
        const auto x = a.next();
        CHECK(x == b.next());
        differs = differs || x != c.next();
    }
    CHECK(differs);
    for (int i = 0; i < 1000; ++i) {
        const int v = a.range(-3, 3);
        CHECK(v >= -3);
        CHECK(v <= 3);
    }
}
