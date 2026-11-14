// Animales de Gaia: ganado que cambia de dueño, presas que huyen y fauna que deambula.
// Lobos (atacan) y jabalíes (se defienden) usan el combate normal: auto_attack_system y la represalia.

#include <algorithm>
#include <array>

#include "sim/game/game_state.hpp"

namespace sim {

using core::Fixed;
using core::Vec2F;

namespace {

constexpr uint32_t kFleeTicks = 2 * kTicksPerSecond;  // tras un golpe, huye a ráfagas de 2 s

}  // namespace

void GameState::convert(core::EntityHandle h, data::PlayerId new_owner) {
    SpawnableEntity* e = entities_.get(h);
    if (!e || e->current_player == new_owner) return;
    const auto& s = sheet_of(*e);
    PlayerState& old_owner = players_[e->current_player];
    std::erase(old_owner.units, h);
    std::erase(old_owner.buildings, h);
    old_owner.population_current -= s.stats.population_cost;
    e->current_player = new_owner;  // original_player no cambia (architecture.md §3)
    e->action.reset();
    e->orders.clear();
    PlayerState& owner = players_[new_owner];
    if (s.is_unit()) owner.units.push_back(h);
    if (s.is_building()) owner.buildings.push_back(h);
    owner.population_current += s.stats.population_cost;
}

void GameState::animal_system() {
    if (tick_ % 4 != 0) return;
    std::vector<core::EntityHandle> near;
    std::vector<core::EntityHandle> to_convert;
    std::vector<data::PlayerId> convert_to;

    entities_.for_each([&](core::EntityHandle h, SpawnableEntity& e) {
        const auto& s = sheet_of(e);
        const auto behavior = s.stats.animal;
        if (behavior == data::AnimalBehavior::None || e.hp <= 0) return;

        // Unidades de jugadores cercanas (por jugador) y la amenaza más próxima
        near.clear();
        const Fixed r = Fixed::from_int(5);
        spatial_.query_rect({e.position.x - r, e.position.y - r}, {e.position.x + r, e.position.y + r}, near);
        std::array<int, data::kMaxPlayers> close{};  // a 3 tiles o menos
        const SpawnableEntity* threat = nullptr;
        Fixed threat_d;
        for (core::EntityHandle o : near) {
            const SpawnableEntity* u = entities_.get(o);
            if (!u || u->current_player == data::kGaia || u->hp <= 0) continue;
            const auto& us = sheet_of(*u);
            if (!us.is_unit() || us.stats.animal != data::AnimalBehavior::None) continue;
            const Fixed d = core::length(u->position - e.position);
            if (d <= Fixed::from_int(3)) ++close[u->current_player];
            // Para un ciervo, la amenaza es una tropa cerca, o cualquiera si acaba de recibir un golpe
            const bool hurt = e.last_damaged_tick <= tick_ && tick_ - e.last_damaged_tick < kFleeTicks;
            const bool scary = hurt ? d <= r : (d <= Fixed::from_int(3) && !has_cap(*u, data::Capability::Gather));
            if (scary && (!threat || d < threat_d || (d == threat_d && o.index < threat->self.index))) {
                threat = u;
                threat_d = d;
            }
        }

        switch (behavior) {
        case data::AnimalBehavior::Domestic: {
            // Se queda con quien llega si solo hay un jugador cerca; si su dueño se aleja y llega otro, cambia
            int present = 0;
            data::PlayerId who = data::kGaia;
            for (int p = 1; p < data::kMaxPlayers; ++p) {
                if (close[static_cast<std::size_t>(p)] > 0) {
                    ++present;
                    who = static_cast<data::PlayerId>(p);
                }
            }
            const bool owner_near = e.current_player != data::kGaia && close[e.current_player] > 0;
            if (present == 1 && who != e.current_player && !owner_near) {
                to_convert.push_back(h);
                convert_to.push_back(who);
            }
            break;
        }
        case data::AnimalBehavior::Flee:
            if (threat && (!e.action || e.action->kind() == ActionKind::Move)) {
                // Correr en dirección contraria a la amenaza
                const Vec2F away = e.position - threat->position;
                const Fixed len = core::length(away);
                Vec2F dir = len > Fixed{} ? Vec2F{away.x * Fixed::from_int(5) / len, away.y * Fixed::from_int(5) / len}
                                          : Vec2F{Fixed::from_int(5), Fixed{}};
                const Fixed max_x = Fixed::from_int(map_.width()) - Fixed::ratio(1, 2);
                const Fixed max_y = Fixed::from_int(map_.height()) - Fixed::ratio(1, 2);
                const Vec2F target{std::clamp(e.position.x + dir.x, Fixed::ratio(1, 2), max_x),
                                   std::clamp(e.position.y + dir.y, Fixed::ratio(1, 2), max_y)};
                e.action = std::make_unique<MoveAction>(target);
                return;
            }
            break;
        default: break;
        }

        // Deambular: los animales salvajes ociosos se mueven un poco de vez en cuando
        if (e.current_player == data::kGaia && !e.action && random(30) == 0) {
            const Vec2F target{e.position.x + Fixed::from_int(static_cast<int32_t>(random(7)) - 3),
                               e.position.y + Fixed::from_int(static_cast<int32_t>(random(7)) - 3)};
            if (map_.is_passable(core::tile_of(target))) e.action = std::make_unique<MoveAction>(target);
        }
    });

    // Cambios de dueño fuera de la iteración (tocan las listas de los jugadores)
    for (std::size_t i = 0; i < to_convert.size(); ++i) convert(to_convert[i], convert_to[i]);
}

}  // namespace sim
