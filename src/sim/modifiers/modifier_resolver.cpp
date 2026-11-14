#include "sim/modifiers/modifier_resolver.hpp"

#include <algorithm>
#include <string>

namespace sim {

using core::Fixed;
using data::ModifierOp;
using data::StatTag;

namespace {

int32_t round_int(Fixed f) { return (f + Fixed::ratio(1, 2)).floor_int(); }

bool touches(std::span<const data::Modifier> mods, StatTag stat, uint8_t sub, data::TypeId type, const data::ClassSet& classes) {
    for (const auto& m : mods) {
        if (!m.applies_to(type, classes)) continue;
        for (const auto& s : m.stats) {
            if (s.stat == stat && s.sub == sub) return true;
        }
    }
    return false;
}

}  // namespace

Fixed resolve_stat(Fixed base, StatTag stat, uint8_t sub, std::span<const data::Modifier> mods, data::TypeId type,
                   const data::ClassSet& classes) {
    Fixed flat;
    Fixed percent;
    Fixed product = Fixed::from_int(1);
    const data::StatModifier* set = nullptr;
    const std::string* set_id = nullptr;

    for (const auto& m : mods) {
        if (!m.applies_to(type, classes)) continue;
        for (const auto& s : m.stats) {
            if (s.stat != stat || s.sub != sub) continue;
            switch (s.op) {
            case ModifierOp::AddFlat: flat += s.value; break;
            case ModifierOp::AddPercent: percent += s.value; break;
            case ModifierOp::MultiplyFlat: product = product * s.value; break;
            case ModifierOp::SetValue:
                if (!set || s.priority > set->priority || (s.priority == set->priority && m.id > *set_id)) {
                    set = &s;
                    set_id = &m.id;
                }
                break;
            }
        }
    }
    if (set) return set->value;
    Fixed v = base + flat;
    v = v * (Fixed::from_int(1) + percent / Fixed::from_int(100));
    return v * product;
}

data::EntityStats compute_effective(const data::ObjSheet& sheet, std::span<const data::Modifier> mods) {
    data::EntityStats s = sheet.stats;
    if (mods.empty()) return s;
    const data::TypeId t = sheet.type;
    const data::ClassSet& c = sheet.stats.classes;
    auto r = [&](Fixed base, StatTag stat, uint8_t sub = 0) { return resolve_stat(base, stat, sub, mods, t, c); };

    s.max_hp = std::max(1, round_int(r(Fixed::from_int(s.max_hp), StatTag::MaxHp)));
    s.move_speed = std::max(Fixed{}, r(s.move_speed, StatTag::MoveSpeed));
    s.range = std::max(Fixed{}, r(s.range, StatTag::Range));
    s.reload_ticks = static_cast<uint32_t>(std::max(1, round_int(r(Fixed::from_int(static_cast<int32_t>(s.reload_ticks)), StatTag::Reload))));
    s.carry_capacity = std::max(0, round_int(r(Fixed::from_int(s.carry_capacity), StatTag::CarryCapacity)));
    s.sight = static_cast<uint8_t>(std::clamp(round_int(r(Fixed::from_int(s.sight), StatTag::Sight)), 0, 255));
    for (std::size_t res = 0; res < data::kResourceCount; ++res) {
        s.gather_per_tick[res] = std::max(Fixed{}, r(s.gather_per_tick[res], StatTag::GatherRate, static_cast<uint8_t>(res)));
    }

    // Ataques y armaduras: también las clases que solo añade un modificador
    // (p. ej. una tecnología que da un bonus nuevo contra caballería).
    auto resolve_list = [&](std::vector<data::DamageEntry>& list, StatTag stat) {
        for (auto& e : list) e.value = round_int(r(Fixed::from_int(e.value), stat, e.armor_class));
        for (uint8_t cls = 0; cls < data::kMaxArmorClasses; ++cls) {
            const bool present = std::any_of(list.begin(), list.end(), [cls](const auto& e) { return e.armor_class == cls; });
            if (present || !touches(mods, stat, cls, t, c)) continue;
            const int32_t v = round_int(r(Fixed{}, stat, cls));
            if (v != 0) list.push_back({cls, v});
        }
    };
    if (!s.attacks.empty()) resolve_list(s.attacks, StatTag::Attack);
    resolve_list(s.armors, StatTag::Armor);
    for (auto& a : s.attacks) a.value = std::max(0, a.value);
    return s;
}

}  // namespace sim
