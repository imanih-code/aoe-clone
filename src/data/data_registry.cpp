#include "data/data_registry.hpp"

#include <algorithm>
#include <fstream>

#include <nlohmann/json.hpp>

namespace data {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

json read_json(const fs::path& path) {
    std::ifstream in(path);
    if (!in) throw DataError(path.string() + ": no se pudo abrir");
    try {
        return json::parse(in, nullptr, /*allow_exceptions=*/true, /*ignore_comments=*/true);
    } catch (const json::parse_error& e) {
        throw DataError(path.string() + ": JSON inválido: " + e.what());
    }
}

// "#RRGGBB" o "#RRGGBBAA" → RGBA
uint32_t parse_color(const std::string& s, const std::string& where) {
    if ((s.size() != 7 && s.size() != 9) || s[0] != '#') {
        throw DataError(where + ": color '" + s + "' debe tener el formato #RRGGBB o #RRGGBBAA");
    }
    uint32_t value = 0;
    try {
        value = static_cast<uint32_t>(std::stoul(s.substr(1), nullptr, 16));
    } catch (const std::exception&) {
        throw DataError(where + ": color '" + s + "' no es hexadecimal");
    }
    return s.size() == 7 ? (value << 8u) | 0xffu : value;
}

ResourceTag parse_resource(const std::string& s, const std::string& where) {
    if (s == "food") return ResourceTag::Food;
    if (s == "wood") return ResourceTag::Wood;
    if (s == "gold") return ResourceTag::Gold;
    if (s == "stone") return ResourceTag::Stone;
    throw DataError(where + ": recurso desconocido '" + s + "' (food, wood, gold, stone)");
}

EntityKind parse_kind(const std::string& s, const std::string& where) {
    if (s == "unit") return EntityKind::Unit;
    if (s == "building") return EntityKind::Building;
    if (s == "resource") return EntityKind::Resource;
    throw DataError(where + ": kind desconocido '" + s + "' (unit, building, resource)");
}

ShapeKind parse_shape(const std::string& s, const std::string& where) {
    if (s == "unit") return ShapeKind::Unit;
    if (s == "cavalry") return ShapeKind::Cavalry;
    if (s == "building") return ShapeKind::Building;
    if (s == "tree") return ShapeKind::Tree;
    if (s == "mine") return ShapeKind::Mine;
    if (s == "bush") return ShapeKind::Bush;
    throw DataError(where + ": shape desconocida '" + s + "' (unit, cavalry, building, tree, mine, bush)");
}

std::vector<fs::path> json_files_in(const fs::path& dir) {
    std::vector<fs::path> files;
    if (!fs::exists(dir)) return files;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());  // orden estable → TypeId estables
    return files;
}

}  // namespace

std::optional<TypeId> DataRegistry::find_type(std::string_view key) const {
    auto it = type_by_key_.find(key);
    if (it == type_by_key_.end()) return std::nullopt;
    return it->second;
}

TypeId DataRegistry::type(std::string_view key) const {
    if (auto t = find_type(key)) return *t;
    throw DataError("tipo de entidad desconocido: '" + std::string(key) + "'");
}

std::optional<ClassId> DataRegistry::find_class(std::string_view name) const {
    auto it = class_by_name_.find(name);
    if (it == class_by_name_.end()) return std::nullopt;
    return it->second;
}

DataRegistry DataRegistry::load(const fs::path& data_dir) {
    DataRegistry reg;

    // ─── Clases ──────────────────────────────────────────────────────────
    {
        const fs::path path = data_dir / "classes.json";
        const json j = read_json(path);
        for (const auto& name : j.at("classes")) {
            const std::string n = name.get<std::string>();
            if (reg.class_names_.size() >= kMaxClasses) throw DataError(path.string() + ": demasiadas clases (máx. 64)");
            if (reg.class_by_name_.count(n)) throw DataError(path.string() + ": clase repetida '" + n + "'");
            reg.class_by_name_.emplace(n, static_cast<ClassId>(reg.class_names_.size()));
            reg.class_names_.push_back(n);
        }
    }

    // ─── Entidades ───────────────────────────────────────────────────────
    const auto files = json_files_in(data_dir / "entities");
    if (files.empty()) throw DataError((data_dir / "entities").string() + ": no hay ficheros .json");

    for (const auto& path : files) {
        const std::string where = path.string();
        const json j = read_json(path);
        try {
            ObjSheet s;
            s.key = j.at("key").get<std::string>();
            if (reg.type_by_key_.count(s.key)) throw DataError(where + ": key repetida '" + s.key + "'");
            s.type = TypeId{static_cast<uint16_t>(reg.sheets_.size())};
            s.display_name = j.value("name", s.key);
            s.kind = parse_kind(j.at("kind").get<std::string>(), where);

            for (const auto& c : j.value("classes", json::array())) {
                const std::string cname = c.get<std::string>();
                auto id = reg.find_class(cname);
                if (!id) throw DataError(where + ": clase '" + cname + "' no está en classes.json");
                s.stats.classes.add(*id);
            }
            s.stats.max_hp = j.value("hp", 1);
            s.stats.move_speed = core::Fixed::from_double(j.value("speed", 0.0));
            s.stats.radius = core::Fixed::from_double(j.value("radius", 0.25));
            s.stats.sight = static_cast<uint8_t>(j.value("sight", 4));
            s.stats.population_cost = j.value("population", 0);
            s.stats.population_provided = j.value("population_provided", 0);
            if (j.contains("footprint")) {
                const auto& fp = j.at("footprint");
                s.stats.footprint_w = static_cast<uint8_t>(fp.at(0).get<int>());
                s.stats.footprint_h = static_cast<uint8_t>(fp.at(1).get<int>());
                if (s.stats.footprint_w == 0 || s.stats.footprint_h == 0) throw DataError(where + ": footprint debe ser >= 1x1");
            }
            if (j.contains("resource")) {
                const auto& r = j.at("resource");
                s.resource = ResourceNodeDef{parse_resource(r.at("type").get<std::string>(), where), r.at("amount").get<int32_t>()};
            }
            const json v = j.value("visual", json::object());
            s.visual.shape = parse_shape(v.value("shape", std::string(s.is_unit() ? "unit" : "building")), where);
            s.visual.size = v.value("size", 0.3f);
            s.visual.height = v.value("height", 1.0f);
            if (v.contains("color")) {
                s.visual.color = parse_color(v.at("color").get<std::string>(), where);
                s.visual.use_player_color = false;
            }
            s.visual.use_player_color = v.value("player_color", s.visual.use_player_color);
            s.visual.label = v.value("label", s.display_name);

            reg.type_by_key_.emplace(s.key, s.type);
            reg.sheets_.push_back(std::move(s));
        } catch (const json::exception& e) {
            throw DataError(where + ": " + e.what());
        }
    }

    // ─── Partida inicial ─────────────────────────────────────────────────
    {
        const fs::path path = data_dir / "start.json";
        const std::string where = path.string();
        const json j = read_json(path);
        try {
            auto type_ref = [&](const json& node) {
                const std::string key = node.at("type").get<std::string>();
                auto t = reg.find_type(key);
                if (!t) throw DataError(where + ": tipo desconocido '" + key + "'");
                return *t;
            };

            const json res = j.value("resources", json::object());
            reg.start_.resources[index_of(ResourceTag::Food)] = res.value("food", 0);
            reg.start_.resources[index_of(ResourceTag::Wood)] = res.value("wood", 0);
            reg.start_.resources[index_of(ResourceTag::Gold)] = res.value("gold", 0);
            reg.start_.resources[index_of(ResourceTag::Stone)] = res.value("stone", 0);

            for (const auto& p : j.at("players")) {
                reg.start_.players.push_back(PlayerDef{p.at("name").get<std::string>(), parse_color(p.at("color").get<std::string>(), where)});
            }
            if (reg.start_.players.empty() || reg.start_.players.size() > kMaxPlayers - 1) {
                throw DataError(where + ": se necesitan entre 1 y 8 jugadores");
            }
            for (const auto& b : j.value("buildings", json::array())) {
                const auto& off = b.at("offset");
                reg.start_.buildings.push_back(StartBuilding{type_ref(b), core::TilePos{off.at(0).get<int>(), off.at(1).get<int>()}});
            }
            for (const auto& u : j.value("units", json::array())) {
                reg.start_.units.push_back(StartUnits{type_ref(u), u.value("count", 1)});
            }
            for (const auto& c : j.value("near_base", json::array())) {
                NearBaseCluster nb;
                nb.type = type_ref(c);
                nb.count_min = c.at("count").at(0).get<int>();
                nb.count_max = c.at("count").at(1).get<int>();
                nb.dist_min = c.at("distance").at(0).get<int>();
                nb.dist_max = c.at("distance").at(1).get<int>();
                reg.start_.near_base.push_back(nb);
            }
            for (const auto& c : j.value("map_wide", json::array())) {
                MapCluster mc;
                mc.type = type_ref(c);
                mc.clusters = c.at("clusters").get<int>();
                mc.size_min = c.at("size").at(0).get<int>();
                mc.size_max = c.at("size").at(1).get<int>();
                mc.min_dist_from_start = c.value("min_distance_from_start", 14);
                reg.start_.map_wide.push_back(mc);
            }
        } catch (const json::exception& e) {
            throw DataError(where + ": " + e.what());
        }
    }

    return reg;
}

}  // namespace data
