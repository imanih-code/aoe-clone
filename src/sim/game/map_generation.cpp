// Generación del mapa inicial: lagos, bases de los jugadores y recursos.
// Todo usa rng_ (semilla de GameConfig) y aritmética entera → mismo mapa en cualquier máquina.

#include <algorithm>
#include <array>
#include <climits>
#include <numeric>

#include "sim/game/game_state.hpp"

namespace sim {

namespace {

// 8 direcciones ×1000 (sin trigonometría en float: determinismo).
constexpr int kDirs[8][2] = {{1000, 0}, {707, 707}, {0, 1000}, {-707, 707}, {-1000, 0}, {-707, -707}, {0, -1000}, {707, -707}};

}  // namespace

void GameState::setup_players() {
    const auto& start = data_.start();
    PlayerState& gaia = players_[data::kGaia];
    gaia.id = data::kGaia;
    gaia.name = "Gaia";
    gaia.color = 0xb0b0b0ffu;
    gaia.active = true;

    for (int p = 1; p <= config_.num_players; ++p) {
        PlayerState& ps = players_[static_cast<std::size_t>(p)];
        const auto& def = start.players[static_cast<std::size_t>(p - 1)];
        ps.id = static_cast<data::PlayerId>(p);
        ps.name = def.name;
        ps.color = def.color;
        ps.active = true;
        ps.resources = start.resources;
    }
}

void GameState::generate_map() {
    place_start_positions();
    generate_lakes();
    place_player_bases();
    place_near_base_resources();
    place_map_wide_resources();
}

int GameState::distance_to_nearest_start(core::TilePos t) const {
    int best = INT_MAX;
    for (int p = 1; p <= config_.num_players; ++p) {
        const core::TilePos s = core::tile_of(start_positions_[static_cast<std::size_t>(p)]);
        const int dx = t.x - s.x;
        const int dy = t.y - s.y;
        best = std::min(best, static_cast<int>(core::isqrt64(static_cast<uint64_t>(dx * dx + dy * dy))));
    }
    return best;
}

void GameState::place_start_positions() {
    const int cx = map_.width() / 2;
    const int cy = map_.height() / 2;
    const int radius = std::min(map_.width(), map_.height()) * 34 / 100;
    const int offset = static_cast<int>(rng_.below(8));
    const int n = config_.num_players;
    for (int p = 1; p <= n; ++p) {
        const int k = (offset + (p - 1) * 8 / n) % 8;
        const core::TilePos t{cx + kDirs[k][0] * radius / 1000, cy + kDirs[k][1] * radius / 1000};
        start_positions_[static_cast<std::size_t>(p)] = core::tile_center(t);
    }
}

void GameState::generate_lakes() {
    const int w = map_.width();
    const int h = map_.height();
    const int lakes = std::max(1, (w * h) / 1800);

    for (int l = 0; l < lakes; ++l) {
        core::TilePos center{};
        bool ok = false;
        for (int attempt = 0; attempt < 60 && !ok; ++attempt) {
            center = {rng_.range(6, w - 7), rng_.range(6, h - 7)};
            ok = distance_to_nearest_start(center) >= 22;
        }
        if (!ok) continue;

        const int size = rng_.range(30, 110);
        std::vector<core::TilePos> frontier{center};
        int count = 0;
        while (count < size && !frontier.empty()) {
            const std::size_t i = rng_.below(static_cast<uint32_t>(frontier.size()));
            const core::TilePos t = frontier[i];
            frontier[i] = frontier.back();
            frontier.pop_back();
            if (!map_.in_bounds(t) || map_.terrain(t.x, t.y) == Terrain::Water) continue;
            if (distance_to_nearest_start(t) < 16) continue;
            map_.set_terrain(t.x, t.y, Terrain::Water);
            ++count;
            frontier.push_back({t.x + 1, t.y});
            frontier.push_back({t.x - 1, t.y});
            frontier.push_back({t.x, t.y + 1});
            frontier.push_back({t.x, t.y - 1});
        }
    }

    // Orillas de arena (sobre una copia para no encadenar)
    std::vector<core::TilePos> shore;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (map_.terrain(x, y) == Terrain::Water) continue;
            bool near_water = false;
            for (int dy = -1; dy <= 1 && !near_water; ++dy) {
                for (int dx = -1; dx <= 1 && !near_water; ++dx) {
                    near_water = map_.in_bounds(x + dx, y + dy) && map_.terrain(x + dx, y + dy) == Terrain::Water;
                }
            }
            if (near_water) shore.push_back({x, y});
        }
    }
    for (core::TilePos t : shore) map_.set_terrain(t.x, t.y, Terrain::Sand);
}

void GameState::place_player_bases() {
    const auto& start = data_.start();
    for (int p = 1; p <= config_.num_players; ++p) {
        const auto pid = static_cast<data::PlayerId>(p);
        const core::TilePos c = core::tile_of(start_positions_[pid]);

        // Tierra pisada alrededor de la base
        for (int dy = -7; dy <= 7; ++dy) {
            for (int dx = -7; dx <= 7; ++dx) {
                if (dx * dx + dy * dy > 49 || !map_.in_bounds(c.x + dx, c.y + dy)) continue;
                if (map_.terrain(c.x + dx, c.y + dy) == Terrain::Grass) map_.set_terrain(c.x + dx, c.y + dy, Terrain::Dirt);
            }
        }

        for (const auto& b : start.buildings) {
            const auto& sheet = data_.sheet(b.type);
            const core::TilePos tile{c.x - sheet.stats.footprint_w / 2 + b.offset.x, c.y - sheet.stats.footprint_h / 2 + b.offset.y};
            spawn_now({b.type, pid, tile});
        }

        int u = 0;
        for (const auto& group : start.units) {
            for (int i = 0; i < group.count; ++i, ++u) {
                spawn_now({group.type, pid, core::TilePos{c.x - 2 + (u % 5), c.y + 3 + u / 5}});
            }
        }
    }
}

int GameState::place_cluster(data::TypeId type, core::TilePos origin, int count, int min_dist_from_start) {
    auto start_tile = Pathfinder::nearest_passable(map_, origin, 6);
    if (!start_tile) return 0;

    std::vector<uint8_t> visited(static_cast<std::size_t>(map_.width() * map_.height()), 0);
    std::vector<core::TilePos> frontier{*start_tile};
    int placed = 0;
    int guard = 0;
    while (placed < count && !frontier.empty() && guard++ < count * 30) {
        const std::size_t i = rng_.below(static_cast<uint32_t>(frontier.size()));
        const core::TilePos t = frontier[i];
        frontier[i] = frontier.back();
        frontier.pop_back();
        if (!map_.in_bounds(t)) continue;
        uint8_t& seen = visited[static_cast<std::size_t>(t.y * map_.width() + t.x)];
        if (seen) continue;
        seen = 1;
        if (distance_to_nearest_start(t) < min_dist_from_start) continue;
        if (!spawn_now({type, data::kGaia, t})) continue;
        ++placed;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx != 0 || dy != 0) frontier.push_back({t.x + dx, t.y + dy});
            }
        }
    }
    return placed;
}

void GameState::place_near_base_resources() {
    const auto& start = data_.start();
    for (int p = 1; p <= config_.num_players; ++p) {
        const core::TilePos c = core::tile_of(start_positions_[static_cast<std::size_t>(p)]);

        // Cada grupo en una dirección distinta alrededor de la base
        std::array<int, 8> order{};
        std::iota(order.begin(), order.end(), 0);
        for (int i = 7; i > 0; --i) std::swap(order[static_cast<std::size_t>(i)], order[rng_.below(static_cast<uint32_t>(i + 1))]);

        std::size_t d = 0;
        for (const auto& cluster : start.near_base) {
            const int k = order[d++ % order.size()];
            const int dist = rng_.range(cluster.dist_min, cluster.dist_max);
            const core::TilePos origin{c.x + kDirs[k][0] * dist / 1000 + rng_.range(-1, 1),
                                       c.y + kDirs[k][1] * dist / 1000 + rng_.range(-1, 1)};
            place_cluster(cluster.type, origin, rng_.range(cluster.count_min, cluster.count_max), 5);
        }
    }
}

void GameState::place_map_wide_resources() {
    for (const auto& mc : data_.start().map_wide) {
        for (int i = 0; i < mc.clusters; ++i) {
            for (int attempt = 0; attempt < 40; ++attempt) {
                const core::TilePos origin{rng_.range(2, map_.width() - 3), rng_.range(2, map_.height() - 3)};
                if (distance_to_nearest_start(origin) < mc.min_dist_from_start) continue;
                place_cluster(mc.type, origin, rng_.range(mc.size_min, mc.size_max), std::max(5, mc.min_dist_from_start - 4));
                break;
            }
        }
    }
}

}  // namespace sim
