#include "data/data_registry.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>

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

// Debe coincidir con sim::kTicksPerSecond (data no depende de sim).
constexpr double kTicksPerSecondData = 20.0;

ResourceArray parse_resources(const json& j, const std::string& where) {
    ResourceArray out{};
    for (const auto& [name, amount] : j.items()) out[index_of(parse_resource(name, where))] = amount.get<int32_t>();
    return out;
}

EntityKind parse_kind(const std::string& s, const std::string& where) {
    if (s == "unit") return EntityKind::Unit;
    if (s == "building") return EntityKind::Building;
    if (s == "resource") return EntityKind::Resource;
    if (s == "projectile") return EntityKind::Projectile;
    throw DataError(where + ": kind desconocido '" + s + "' (unit, building, resource, projectile)");
}

ShapeKind parse_shape(const std::string& s, const std::string& where) {
    if (s == "unit") return ShapeKind::Unit;
    if (s == "cavalry") return ShapeKind::Cavalry;
    if (s == "building") return ShapeKind::Building;
    if (s == "tree") return ShapeKind::Tree;
    if (s == "mine") return ShapeKind::Mine;
    if (s == "bush") return ShapeKind::Bush;
    if (s == "arrow") return ShapeKind::Arrow;
    throw DataError(where + ": shape desconocida '" + s + "' (unit, cavalry, building, tree, mine, bush, arrow)");
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

std::optional<ArmorClassId> DataRegistry::find_armor_class(std::string_view name) const {
    for (std::size_t i = 0; i < armor_class_names_.size(); ++i) {
        if (armor_class_names_[i] == name) return static_cast<ArmorClassId>(i);
    }
    return std::nullopt;
}

std::optional<TechId> DataRegistry::find_tech(std::string_view key) const {
    auto it = tech_by_key_.find(key);
    if (it == tech_by_key_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::size_t> DataRegistry::find_civ(std::string_view key) const {
    for (std::size_t i = 0; i < civs_.size(); ++i) {
        if (civs_[i].key == key) return i;
    }
    return std::nullopt;
}

// { "id": "...", "type": "villager" | "classes": [...],
//   "stats": [ { "stat": "attack", "class": "melee", "op": "add", "value": 1 }, ... ] }
Modifier DataRegistry::parse_modifier(const json& j, const std::string& where) const {
    Modifier m;
    m.id = j.value("id", std::string());
    if (j.contains("type")) m.target_type = type(j.at("type").get<std::string>());
    for (const auto& c : j.value("classes", json::array())) {
        auto id = find_class(c.get<std::string>());
        if (!id) throw DataError(where + ": modificador con clase desconocida '" + c.get<std::string>() + "'");
        m.target_classes.add(*id);
    }
    for (const auto& s : j.at("stats")) {
        StatModifier sm;
        const std::string stat = s.at("stat").get<std::string>();
        if (stat == "max_hp") sm.stat = StatTag::MaxHp;
        else if (stat == "attack") sm.stat = StatTag::Attack;
        else if (stat == "armor") sm.stat = StatTag::Armor;
        else if (stat == "move_speed") sm.stat = StatTag::MoveSpeed;
        else if (stat == "range") sm.stat = StatTag::Range;
        else if (stat == "reload") sm.stat = StatTag::Reload;
        else if (stat == "gather_rate") sm.stat = StatTag::GatherRate;
        else if (stat == "carry_capacity") sm.stat = StatTag::CarryCapacity;
        else if (stat == "sight") sm.stat = StatTag::Sight;
        else throw DataError(where + ": stat desconocido '" + stat + "'");

        if (sm.stat == StatTag::Attack || sm.stat == StatTag::Armor) {
            auto cls = find_armor_class(s.at("class").get<std::string>());
            if (!cls) throw DataError(where + ": clase de armadura desconocida '" + s.at("class").get<std::string>() + "'");
            sm.sub = *cls;
        }
        if (sm.stat == StatTag::GatherRate) sm.sub = static_cast<uint8_t>(parse_resource(s.at("resource").get<std::string>(), where));

        const std::string op = s.value("op", std::string("add"));
        if (op == "add") sm.op = ModifierOp::AddFlat;
        else if (op == "add_percent") sm.op = ModifierOp::AddPercent;
        else if (op == "multiply") sm.op = ModifierOp::MultiplyFlat;
        else if (op == "set") sm.op = ModifierOp::SetValue;
        else throw DataError(where + ": op desconocida '" + op + "' (add, add_percent, multiply, set)");

        double value = s.at("value").get<double>();
        // Valores absolutos en las mismas unidades que las fichas (segundos, por segundo)
        if (sm.op == ModifierOp::AddFlat || sm.op == ModifierOp::SetValue) {
            if (sm.stat == StatTag::Reload) value *= kTicksPerSecondData;
            if (sm.stat == StatTag::GatherRate) value /= kTicksPerSecondData;
        }
        sm.value = core::Fixed::from_double(value);
        sm.priority = static_cast<int16_t>(s.value("priority", 0));
        m.stats.push_back(sm);
    }
    return m;
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
        // Clases de armadura: "melee" y "pierce" siempre existen (ataques básicos)
        reg.armor_class_names_ = {"melee", "pierce"};
        for (const auto& name : j.value("armor_classes", json::array())) {
            const std::string n = name.get<std::string>();
            if (reg.find_armor_class(n)) continue;
            if (reg.armor_class_names_.size() >= kMaxArmorClasses) throw DataError(path.string() + ": demasiadas clases de armadura");
            reg.armor_class_names_.push_back(n);
        }
    }

    // ─── Edades ──────────────────────────────────────────────────────────
    if (fs::exists(data_dir / "ages.json")) {
        const json j = read_json(data_dir / "ages.json");
        for (const auto& a : j.at("ages")) reg.age_names_.push_back(a.get<std::string>());
    }
    if (reg.age_names_.empty()) reg.age_names_.push_back("Edad única");

    // ─── Entidades ───────────────────────────────────────────────────────
    // 1) Leer todos los ficheros (incluidas plantillas abstractas)
    // 2) Resolver "extends" con JSON merge patch (architecture.md §11.8)
    // 3) Crear fichas de las no abstractas, en orden de fichero → TypeId estables
    // 4) Resolver referencias entre fichas (trains, builds)
    const auto files = json_files_in(data_dir / "entities");
    if (files.empty()) throw DataError((data_dir / "entities").string() + ": no hay ficheros .json");

    struct RawEntity {
        json body;
        std::string file;
    };
    std::map<std::string, RawEntity, std::less<>> raw;
    std::vector<std::string> order;
    for (const auto& path : files) {
        json j = read_json(path);
        if (!j.contains("key") || !j.at("key").is_string()) throw DataError(path.string() + ": falta \"key\"");
        const std::string key = j.at("key").get<std::string>();
        if (raw.count(key)) throw DataError(path.string() + ": key repetida '" + key + "' (ya está en " + raw.at(key).file + ")");
        raw.emplace(key, RawEntity{std::move(j), path.string()});
        order.push_back(key);
    }

    std::map<std::string, json, std::less<>> resolved;
    std::function<const json&(const std::string&, std::vector<std::string>&)> resolve =
        [&](const std::string& key, std::vector<std::string>& chain) -> const json& {
        if (auto it = resolved.find(key); it != resolved.end()) return it->second;
        const RawEntity& r = raw.at(key);
        if (std::find(chain.begin(), chain.end(), key) != chain.end()) {
            std::string cycle;
            for (const auto& c : chain) cycle += c + " → ";
            throw DataError(r.file + ": herencia circular: " + cycle + key);
        }
        json result;
        if (r.body.contains("extends")) {
            const std::string parent = r.body.at("extends").get<std::string>();
            if (!raw.count(parent)) throw DataError(r.file + ": extends '" + parent + "' no existe");
            chain.push_back(key);
            result = resolve(parent, chain);
            chain.pop_back();
            result.erase("abstract");  // ser abstracto no se hereda
            result.merge_patch(r.body);
        } else {
            result = r.body;
        }
        result.erase("extends");
        return resolved.emplace(key, std::move(result)).first->second;
    };

    std::vector<std::pair<std::vector<std::string>, std::vector<std::string>>> pending_refs;  // trains, builds
    std::vector<std::string> pending_projectiles;
    for (const auto& key : order) {
        std::vector<std::string> chain;
        const json& j = resolve(key, chain);
        if (j.value("abstract", false)) continue;
        const std::string where = raw.at(key).file;
        try {
            ObjSheet s;
            s.key = key;
            s.type = TypeId{static_cast<uint16_t>(reg.sheets_.size())};
            s.display_name = j.value("name", s.key);
            s.description = j.value("description", std::string());
            s.kind = parse_kind(j.at("kind").get<std::string>(), where);

            for (const auto& c : j.value("classes", json::array())) {
                const std::string cname = c.get<std::string>();
                auto id = reg.find_class(cname);
                if (!id) throw DataError(where + ": clase '" + cname + "' no está en classes.json");
                s.stats.classes.add(*id);
            }
            for (const auto& c : j.value("capabilities", json::array())) {
                const std::string cap = c.get<std::string>();
                if (cap == "move") s.stats.capabilities.set(static_cast<std::size_t>(Capability::Move));
                else if (cap == "gather") s.stats.capabilities.set(static_cast<std::size_t>(Capability::Gather));
                else if (cap == "build") s.stats.capabilities.set(static_cast<std::size_t>(Capability::Build));
                else if (cap == "attack") s.stats.capabilities.set(static_cast<std::size_t>(Capability::Attack));
                else throw DataError(where + ": capability desconocida '" + cap + "' (move, gather, build, attack)");
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

            // Tiempos en segundos en el JSON → ticks
            auto seconds_to_ticks = [](double s) { return static_cast<uint32_t>(std::llround(s * kTicksPerSecondData)); };
            s.stats.train_ticks = seconds_to_ticks(j.value("train_time", 0.0));
            s.stats.build_ticks = seconds_to_ticks(j.value("build_time", 0.0));
            if (j.contains("cost")) s.stats.cost = parse_resources(j.at("cost"), where);

            if (j.contains("gather")) {
                const json& g = j.at("gather");
                s.stats.carry_capacity = g.value("carry", 10);
                for (const auto& [res, rate] : g.at("rates").items()) {
                    const auto tag = parse_resource(res, where);
                    s.stats.gather_per_tick[index_of(tag)] = core::Fixed::from_double(rate.get<double>() / kTicksPerSecondData);
                }
            }
            for (const auto& r : j.value("dropoff", json::array())) {
                s.stats.dropoff.set(index_of(parse_resource(r.get<std::string>(), where)));
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

            if (s.stats.has(Capability::Build) && !j.contains("builds")) {
                throw DataError(where + ": tiene la capability build pero no lista \"builds\"");
            }

            // ─── Combate ─────────────────────────────────────────────────
            auto damage_list = [&](const char* field) {
                std::vector<DamageEntry> out;
                const json entries = j.value(field, json::object());  // variable local: .items() sobre un temporal quedaría colgando
                for (const auto& [cls, value] : entries.items()) {
                    auto id = reg.find_armor_class(cls);
                    if (!id) throw DataError(where + ": " + field + ": clase de armadura '" + cls + "' no está en classes.json");
                    out.push_back({*id, value.get<int32_t>()});
                }
                return out;
            };
            s.stats.attacks = damage_list("attack");
            s.stats.armors = damage_list("armor");
            if (!s.is_projectile() && !s.is_resource()) {
                // Toda entidad atacable declara armadura melee y pierce (aunque sea 0): architecture.md §4.5
                for (ArmorClassId basic : {ArmorClassId{0}, ArmorClassId{1}}) {
                    const bool present = std::any_of(s.stats.armors.begin(), s.stats.armors.end(),
                                                     [basic](const DamageEntry& d) { return d.armor_class == basic; });
                    if (!present) s.stats.armors.push_back({basic, 0});
                }
            }
            s.stats.reload_ticks = std::max<uint32_t>(1, seconds_to_ticks(j.value("reload", 2.0)));
            s.stats.range = core::Fixed::from_double(j.value("range", 0.0));
            s.stats.min_range = core::Fixed::from_double(j.value("min_range", 0.0));
            s.stats.aggressive = j.value("aggressive", false);
            s.stats.min_age = static_cast<uint8_t>(j.value("age", 0));
            if (s.stats.min_age >= reg.age_names_.size()) throw DataError(where + ": age fuera de rango (ver ages.json)");
            if (s.stats.has(Capability::Attack) && s.stats.attacks.empty()) {
                throw DataError(where + ": tiene la capability attack pero no define \"attack\"");
            }

            pending_refs.emplace_back(j.value("trains", std::vector<std::string>{}), j.value("builds", std::vector<std::string>{}));
            pending_projectiles.push_back(j.value("projectile", std::string()));
            reg.type_by_key_.emplace(s.key, s.type);
            reg.sheets_.push_back(std::move(s));
        } catch (const json::exception& e) {
            throw DataError(where + ": " + e.what());
        }
    }

    for (std::size_t i = 0; i < reg.sheets_.size(); ++i) {
        ObjSheet& s = reg.sheets_[i];
        const std::string& where = raw.at(s.key).file;
        auto refs = [&](const std::vector<std::string>& keys, EntityKind expected, const char* field) {
            std::vector<TypeId> out;
            for (const auto& k : keys) {
                auto t = reg.find_type(k);
                if (!t) throw DataError(where + ": " + field + " referencia '" + k + "', que no existe o es abstracta");
                if (reg.sheets_[t->value].kind != expected) throw DataError(where + ": " + field + " → '" + k + "' no es del tipo esperado");
                out.push_back(*t);
            }
            return out;
        };
        s.trains = refs(pending_refs[i].first, EntityKind::Unit, "trains");
        s.builds = refs(pending_refs[i].second, EntityKind::Building, "builds");
        if (!pending_projectiles[i].empty()) s.stats.projectile = refs({pending_projectiles[i]}, EntityKind::Projectile, "projectile").front();
    }

    // ─── Tecnologías (data/techs) ────────────────────────────────────────
    {
        std::vector<std::pair<json, std::string>> raw_techs;
        for (const auto& path : json_files_in(data_dir / "techs")) {
            json j = read_json(path);
            const std::string key = j.at("key").get<std::string>();
            if (reg.tech_by_key_.count(key)) throw DataError(path.string() + ": tecnología repetida '" + key + "'");
            reg.tech_by_key_.emplace(key, static_cast<TechId>(raw_techs.size()));
            raw_techs.emplace_back(std::move(j), path.string());
        }
        for (const auto& [j, where] : raw_techs) {
            try {
                TechTemplate t;
                t.id = static_cast<TechId>(reg.techs_.size());
                t.key = j.at("key").get<std::string>();
                t.name = j.value("name", t.key);
                t.description = j.value("description", std::string());
                const std::string at = j.at("researched_at").get<std::string>();
                auto at_type = reg.find_type(at);
                if (!at_type || !reg.sheets_[at_type->value].is_building()) throw DataError(where + ": researched_at '" + at + "' no es un edificio");
                t.researched_at = *at_type;
                t.cost = parse_resources(j.value("cost", json::object()), where);
                t.research_ticks = std::max<uint32_t>(1, static_cast<uint32_t>(std::llround(j.value("time", 30.0) * kTicksPerSecondData)));
                t.min_age = static_cast<uint8_t>(j.value("age", 0));
                for (const auto& r : j.value("requires", json::array())) {
                    auto id = reg.find_tech(r.get<std::string>());
                    if (!id) throw DataError(where + ": requires '" + r.get<std::string>() + "' no existe");
                    t.requires_techs.push_back(*id);
                }
                for (const auto& m : j.value("modifiers", json::array())) t.modifiers.push_back(reg.parse_modifier(m, where));
                for (const auto& u : j.value("upgrades", json::array())) {
                    auto from = reg.find_type(u.at("from").get<std::string>());
                    auto to = reg.find_type(u.at("to").get<std::string>());
                    if (!from || !to) throw DataError(where + ": upgrades con un tipo inexistente");
                    t.upgrades.push_back({*from, *to});
                }
                if (j.contains("advances_to_age")) {
                    const int age = j.at("advances_to_age").get<int>();
                    if (age <= 0 || static_cast<std::size_t>(age) >= reg.age_names_.size()) throw DataError(where + ": advances_to_age fuera de rango");
                    t.advances_to_age = static_cast<uint8_t>(age);
                }
                reg.techs_.push_back(std::move(t));
            } catch (const json::exception& e) {
                throw DataError(where + ": " + e.what());
            }
        }
    }

    // ─── Civilizaciones (data/civs) ──────────────────────────────────────
    for (const auto& path : json_files_in(data_dir / "civs")) {
        const std::string where = path.string();
        const json j = read_json(path);
        try {
            CivTemplate c;
            c.key = j.at("key").get<std::string>();
            if (reg.find_civ(c.key)) throw DataError(where + ": civilización repetida '" + c.key + "'");
            c.name = j.value("name", c.key);
            c.description = j.value("description", std::string());
            for (const auto& m : j.value("bonuses", json::array())) c.bonuses.push_back(reg.parse_modifier(m, where));
            for (const auto& k : j.value("disabled_types", json::array())) c.disabled_types.push_back(reg.type(k.get<std::string>()));
            for (const auto& k : j.value("disabled_techs", json::array())) {
                auto id = reg.find_tech(k.get<std::string>());
                if (!id) throw DataError(where + ": disabled_techs '" + k.get<std::string>() + "' no existe");
                c.disabled_techs.push_back(*id);
            }
            const json extra = j.value("extra_trains", json::object());
            for (const auto& [building, units] : extra.items()) {
                const TypeId b = reg.type(building);
                for (const auto& u : units) c.extra_trains.push_back({b, reg.type(u.get<std::string>())});
            }
            reg.civs_.push_back(std::move(c));
        } catch (const json::exception& e) {
            throw DataError(where + ": " + e.what());
        }
    }
    if (reg.civs_.empty()) reg.civs_.push_back(CivTemplate{"default", "Sin civilización", "", {}, {}, {}, {}});

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

            reg.start_.resources = parse_resources(j.value("resources", json::object()), where);

            for (const auto& p : j.at("players")) {
                PlayerDef def{p.at("name").get<std::string>(), parse_color(p.at("color").get<std::string>(), where), p.value("civ", std::string())};
                if (!def.civ.empty() && !reg.find_civ(def.civ)) throw DataError(where + ": civilización desconocida '" + def.civ + "'");
                reg.start_.players.push_back(std::move(def));
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
