#include "sim/world/pathfinder.hpp"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <queue>

namespace sim {

namespace {

constexpr int32_t kStraight = 10;
constexpr int32_t kDiagonal = 14;

struct Dir {
    int dx;
    int dy;
};
// Orden fijo: el resultado no depende de nada externo.
constexpr Dir kDirs[8] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1}};

int32_t octile(core::TilePos a, core::TilePos b) {
    const int32_t dx = std::abs(a.x - b.x);
    const int32_t dy = std::abs(a.y - b.y);
    return kStraight * std::max(dx, dy) + (kDiagonal - kStraight) * std::min(dx, dy);
}

struct OpenNode {
    int32_t f;
    int32_t h;
    int32_t index;
    // priority_queue es un max-heap: "mayor" = peor
    bool operator<(const OpenNode& o) const {
        if (f != o.f) return f > o.f;
        if (h != o.h) return h > o.h;
        return index > o.index;
    }
};

}  // namespace

void Pathfinder::prepare(const TileMap& map) {
    if (map.width() != width_ || map.height() != height_) {
        width_ = map.width();
        height_ = map.height();
        const auto n = static_cast<std::size_t>(width_ * height_);
        seen_.assign(n, 0);
        closed_.assign(n, 0);
        g_.assign(n, 0);
        parent_.assign(n, -1);
        stamp_ = 0;
    }
    if (++stamp_ == 0) {  // desbordamiento tras 4 mil millones de búsquedas
        std::fill(seen_.begin(), seen_.end(), 0);
        std::fill(closed_.begin(), closed_.end(), 0);
        stamp_ = 1;
    }
}

std::optional<core::TilePos> Pathfinder::nearest_passable(const TileMap& map, core::TilePos t, int max_radius, const PathQuery& query) {
    if (query.believes_passable(map, t)) return t;
    // Anillos crecientes alrededor de t; dentro de cada anillo, orden fijo.
    for (int r = 1; r <= max_radius; ++r) {
        std::optional<core::TilePos> best;
        int32_t best_d = 0;
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const core::TilePos c{t.x + dx, t.y + dy};
                if (!query.believes_passable(map, c)) continue;
                const int32_t d = dx * dx + dy * dy;
                if (!best || d < best_d) {
                    best = c;
                    best_d = d;
                }
            }
        }
        if (best) return best;
    }
    return std::nullopt;
}

std::optional<Pathfinder::Result> Pathfinder::find(const TileMap& map, core::TilePos start, core::TilePos goal, const PathQuery& query,
                                                    int max_expansions) {
    auto resolved = nearest_passable(map, goal, 16, query);
    if (!resolved) return std::nullopt;
    if (!map.in_bounds(start)) return std::nullopt;

    prepare(map);
    const core::TilePos target = *resolved;
    auto idx = [&](core::TilePos p) { return p.y * width_ + p.x; };
    auto pos = [&](int32_t i) { return core::TilePos{i % width_, i / width_}; };

    Result result;
    result.goal = target;
    if (start == target) {
        result.reached_goal = true;
        return result;
    }

    std::priority_queue<OpenNode> open;
    const int32_t s = idx(start);
    seen_[s] = stamp_;
    g_[s] = 0;
    parent_[s] = -1;
    open.push({octile(start, target), octile(start, target), s});

    int32_t best_index = s;
    int32_t best_h = octile(start, target);
    int expansions = 0;
    bool found = false;

    while (!open.empty()) {
        const OpenNode node = open.top();
        open.pop();
        if (closed_[node.index] == stamp_) continue;
        closed_[node.index] = stamp_;

        const core::TilePos p = pos(node.index);
        if (p == target) {
            best_index = node.index;
            found = true;
            break;
        }
        if (node.h < best_h || (node.h == best_h && g_[node.index] < g_[best_index])) {
            best_h = node.h;
            best_index = node.index;
        }
        if (++expansions > max_expansions) break;

        for (const Dir& d : kDirs) {
            const core::TilePos n{p.x + d.dx, p.y + d.dy};
            if (!query.believes_passable(map, n)) continue;
            const bool diagonal = d.dx != 0 && d.dy != 0;
            if (diagonal && (!query.believes_passable(map, {p.x + d.dx, p.y}) || !query.believes_passable(map, {p.x, p.y + d.dy}))) continue;
            const int32_t ni = idx(n);
            if (closed_[ni] == stamp_) continue;
            const int32_t g = g_[node.index] + (diagonal ? kDiagonal : kStraight);
            if (seen_[ni] != stamp_ || g < g_[ni]) {
                seen_[ni] = stamp_;
                g_[ni] = g;
                parent_[ni] = node.index;
                const int32_t h = octile(n, target);
                open.push({g + h, h, ni});
            }
        }
    }

    result.reached_goal = found;
    if (!found) result.goal = pos(best_index);
    for (int32_t i = best_index; i != s && i != -1; i = parent_[i]) result.tiles.push_back(pos(i));
    std::reverse(result.tiles.begin(), result.tiles.end());
    return result;
}

bool line_is_clear(const TileMap& map, core::Vec2F a, core::Vec2F b, const PathQuery& query) {
    const core::Vec2F d = b - a;
    const core::Fixed len = core::length(d);
    const int steps = std::max(1, (len * core::Fixed::from_int(4)).floor_int() + 1);
    const core::TilePos start_tile = core::tile_of(a);
    for (int i = 1; i <= steps; ++i) {
        const core::Vec2F p{
            core::Fixed::from_raw(a.x.raw + static_cast<int32_t>(static_cast<int64_t>(d.x.raw) * i / steps)),
            core::Fixed::from_raw(a.y.raw + static_cast<int32_t>(static_cast<int64_t>(d.y.raw) * i / steps))};
        const core::TilePos t = core::tile_of(p);
        if (t == start_tile) continue;  // la unidad puede estar dentro de un tile bloqueado
        if (!query.believes_passable(map, t)) return false;
    }
    return true;
}

}  // namespace sim
