#include "data/data_registry.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <type_traits>
#include <variant>

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
    if (s == "animal") return ShapeKind::Animal;
    if (s == "carcass") return ShapeKind::Carcass;
    if (s == "fish") return ShapeKind::Fish;
    if (s == "boat") return ShapeKind::Boat;
    if (s == "field") return ShapeKind::Field;
    throw DataError(where + ": shape desconocida '" + s + "' (unit, cavalry, building, tree, mine, bush, arrow, animal, carcass, fish, boat, field)");
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


Capability parse_capability(const std::string& s, const std::string& where) {
    static const std::pair<const char*, Capability> kNames[] = {
        {"move", Capability::Move},       {"gather", Capability::Gather}, {"build", Capability::Build},
        {"attack", Capability::Attack},   {"convert", Capability::Convert}, {"heal", Capability::Heal},
        {"morph", Capability::Morph},     {"garrison", Capability::Garrison}};
    for (const auto& [name, cap] : kNames) {
        if (s == name) return cap;
    }
    throw DataError(where + ": capability desconocida '" + s + "' (move, gather, build, attack, convert, heal, morph, garrison)");
}

}  // namespace

// ─── ActionConfig ────────────────────────────────────────────────────────────

ActionConfig merge_config(const ActionConfig& base, const ActionConfig& extra) {
    if (base.index() != extra.index()) return extra;
    return std::visit(
        [&](const auto& a) -> ActionConfig {
            using T = std::decay_t<decltype(a)>;
            const T& b = std::get<T>(extra);
            T out = a;
            if constexpr (std::is_same_v<T, BuildConfig>) {
                // Unión de lo que puede construir
                for (auto t : b.buildable) {
                    if (std::find(out.buildable.begin(), out.buildable.end(), t) == out.buildable.end()) out.buildable.push_back(t);
                }
            } else if constexpr (std::is_same_v<T, ConvertConfig>) {
                out.classes.bits |= b.classes.bits;
                out.buildings = a.buildings || b.buildings;
                out.time_mult = std::min(a.time_mult, b.time_mult);
                out.range = std::max(a.range, b.range);
            } else if constexpr (std::is_same_v<T, MorphConfig>) {
                out = b;  // la última forma manda
            } else if constexpr (std::is_same_v<T, HealConfig>) {
                out.classes.bits |= b.classes.bits;
                out.range = std::max(a.range, b.range);
                out.per_second = std::max(a.per_second, b.per_second);
            } else if constexpr (std::is_same_v<T, GarrisonConfig>) {
                out.classes.bits |= b.classes.bits;
                out.capacity = std::max(a.capacity, b.capacity);
                out.arrows_per_unit = std::max(a.arrows_per_unit, b.arrows_per_unit);
            }
            return out;
        },
        base);
}

ActionConfig DataRegistry::parse_action_config(Capability cap, const json& j, const std::string& where) const {
    auto classes = [&](const char* field) {
        ClassSet out;
        for (const auto& c : j.value(field, json::array())) {
            auto id = find_class(c.get<std::string>());
            if (!id) throw DataError(where + ": clase desconocida '" + c.get<std::string>() + "'");
            out.add(*id);
        }
        return out;
    };
    switch (cap) {
    case Capability::Build: {
        BuildConfig c;
        for (const auto& k : j.value("buildable", json::array())) c.buildable.push_back(type(k.get<std::string>()));
        return c;
    }
    case Capability::Convert: {
        ConvertConfig c;
        c.classes = classes("classes");
        c.buildings = j.value("buildings", false);
        c.time_mult = core::Fixed::from_double(j.value("time_mult", 1.0));
        c.range = core::Fixed::from_double(j.value("range", 1.0));
        return c;
    }
    case Capability::Morph: {
        MorphConfig c;
        c.target = type(j.at("into").get<std::string>());
        c.consume = j.value("consume", true);
        c.keep_hp_percent = j.value("keep_hp_percent", true);
        return c;
    }
    case Capability::Heal: {
        HealConfig c;
        c.classes = classes("classes");
        c.range = core::Fixed::from_double(j.value("range", 4.0));
        c.per_second = core::Fixed::from_double(j.value("per_second", 1.0));
        return c;
    }
    case Capability::Garrison: {
        GarrisonConfig c;
        c.classes = classes("classes");
        c.capacity = j.value("capacity", 0);
        c.arrows_per_unit = j.value("arrows_per_unit", 1);
        return c;
    }
    default: throw DataError(where + ": esa capability no tiene configuración");
    }
}

const TriggerTemplate* DataRegistry::find_trigger(std::string_view id) const {
    auto it = triggers_.find(id);
    return it == triggers_.end() ? nullptr : &it->second;
}

const SkillTemplate* DataRegistry::find_skill(std::string_view id) const {
    auto it = skills_.find(id);
    return it == skills_.end() ? nullptr : &it->second;
}

void DataRegistry::validate_references(const Modifier& m, const std::string& where) const {
    for (const auto& om : m.object_mods) {
        for (const auto* t : {&om.grant_trigger, &om.remove_trigger}) {
            if (!t->empty() && !find_trigger(*t)) throw DataError(where + ": trigger '" + *t + "' no existe en data/triggers");
        }
        for (const auto* s : {&om.grant_skill, &om.remove_skill}) {
            if (!s->empty() && !find_skill(*s)) throw DataError(where + ": skill '" + *s + "' no existe en data/skills");
        }
    }
}

namespace {
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
    for (const auto& s : j.value("stats", json::array())) {
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
    // Cambios de capacidades, triggers y habilidades
    for (const auto& o : j.value("object_mods", json::array())) {
        ObjectModifier om;
        if (o.contains("grant")) om.grant = parse_capability(o.at("grant").get<std::string>(), where);
        if (o.contains("remove")) om.remove = parse_capability(o.at("remove").get<std::string>(), where);
        if (o.contains("config")) {
            if (!om.grant) throw DataError(where + ": object_mods: config necesita grant");
            om.config = parse_action_config(*om.grant, o.at("config"), where);
        }
        om.grant_trigger = o.value("grant_trigger", std::string());
        om.remove_trigger = o.value("remove_trigger", std::string());
        om.grant_skill = o.value("grant_skill", std::string());
        om.remove_skill = o.value("remove_skill", std::string());
        m.object_mods.push_back(std::move(om));
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
    struct PendingConfig {
        std::size_t sheet;
        Capability cap;
        json body;
    };
    std::vector<PendingConfig> pending_configs;  // pueden referenciar tipos que aún no se han cargado
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
                s.stats.capabilities.set(static_cast<std::size_t>(parse_capability(c.get<std::string>(), where)));
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
            {
                const std::string place = j.value("placement", std::string("land"));
                if (place == "land") s.stats.placement = PlacementRule::Land;
                else if (place == "water") s.stats.placement = PlacementRule::Water;
                else if (place == "shore") s.stats.placement = PlacementRule::Shore;
                else throw DataError(where + ": placement desconocido '" + place + "' (land, water, shore)");
                const std::string animal = j.value("animal", std::string("none"));
                if (animal == "none") s.stats.animal = AnimalBehavior::None;
                else if (animal == "domestic") s.stats.animal = AnimalBehavior::Domestic;
                else if (animal == "flee") s.stats.animal = AnimalBehavior::Flee;
                else if (animal == "aggressive") s.stats.animal = AnimalBehavior::Aggressive;
                else if (animal == "retaliate") s.stats.animal = AnimalBehavior::Retaliate;
                else throw DataError(where + ": animal desconocido '" + animal + "' (none, domestic, flee, aggressive, retaliate)");
            }
            s.stats.walkable = j.value("walkable", false);
            s.stats.naval = j.value("naval", false);
            s.stats.gather_slots = std::max(1, j.value("gather_slots", 4));
            s.stats.carcass_food = j.value("carcass_food", 0);
            s.stats.min_age = static_cast<uint8_t>(j.value("age", 0));
            if (s.stats.min_age >= reg.age_names_.size()) throw DataError(where + ": age fuera de rango (ver ages.json)");
            if (s.stats.has(Capability::Attack) && s.stats.attacks.empty()) {
                throw DataError(where + ": tiene la capability attack pero no define \"attack\"");
            }

            pending_refs.emplace_back(j.value("trains", std::vector<std::string>{}), j.value("builds", std::vector<std::string>{}));
            pending_projectiles.push_back(j.value("projectile", std::string()));

            // ─── Mutabilidad (las referencias a triggers y skills se comprueban al final) ─
            const json configs = j.value("action_configs", json::object());  // local: .items() sobre un temporal quedaría colgando
            for (const auto& [cap_name, cfg] : configs.items()) {
                const Capability cap = parse_capability(cap_name, where);
                pending_configs.push_back({reg.sheets_.size(), cap, cfg});
            }
            s.triggers = j.value("triggers", std::vector<std::string>{});
            s.skills = j.value("skills", std::vector<std::string>{});
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


    // Configuración de cada capability (ya se conocen todos los tipos)
    for (const auto& pc : pending_configs) {
        ObjSheet& s = reg.sheets_[pc.sheet];
        s.action_configs.emplace_back(pc.cap, reg.parse_action_config(pc.cap, pc.body, raw.at(s.key).file));
    }

    // ─── Triggers (data/triggers) ────────────────────────────────────────
    for (const auto& path : json_files_in(data_dir / "triggers")) {
        const std::string where = path.string();
        const json j = read_json(path);
        try {
            TriggerTemplate t;
            t.id = j.at("id").get<std::string>();
            const std::string ev = j.at("event").get<std::string>();
            if (ev == "on_death") t.event = TriggerEvent::OnDeath;
            else if (ev == "on_kill") t.event = TriggerEvent::OnKill;
            else if (ev == "on_hit") t.event = TriggerEvent::OnHit;
            else if (ev == "on_spawn") t.event = TriggerEvent::OnSpawn;
            else if (ev == "on_convert") t.event = TriggerEvent::OnConvert;
            else throw DataError(where + ": evento desconocido '" + ev + "' (on_death, on_kill, on_hit, on_spawn, on_convert)");
            for (const auto& c : j.value("filter_classes", json::array())) {
                auto id = reg.find_class(c.get<std::string>());
                if (!id) throw DataError(where + ": clase desconocida '" + c.get<std::string>() + "'");
                t.filter_classes.add(*id);
            }
            t.probability_permille = static_cast<uint16_t>(std::clamp(j.value("probability", 1.0), 0.0, 1.0) * 1000.0 + 0.5);
            t.charges = j.value("charges", -1);
            for (const auto& sp : j.value("spawn", json::array())) {
                SpawnEffect e;
                e.type = reg.type(sp.at("type").get<std::string>());
                e.count = sp.value("count", 1);
                e.inherit_original = sp.value("inherit_original", true);
                e.inherit_current = sp.value("inherit_current", true);
                e.hp_fraction = core::Fixed::from_double(sp.value("hp_fraction", 1.0));
                t.spawns.push_back(e);
            }
            for (const auto& r : j.value("resources", json::array())) {
                t.resources.push_back({parse_resource(r.at("type").get<std::string>(), where), r.at("amount").get<int32_t>(),
                                       r.value("to_original", false)});
            }
            for (const auto& m : j.value("self_modifiers", json::array())) t.self_modifiers.push_back(reg.parse_modifier(m, where));
            if (reg.triggers_.count(t.id)) throw DataError(where + ": trigger repetido '" + t.id + "'");
            reg.triggers_.emplace(t.id, std::move(t));
        } catch (const json::exception& e) {
            throw DataError(where + ": " + e.what());
        }
    }

    // ─── Habilidades (data/skills) ───────────────────────────────────────
    for (const auto& path : json_files_in(data_dir / "skills")) {
        const std::string where = path.string();
        const json j = read_json(path);
        try {
            SkillTemplate s;
            s.id = j.at("id").get<std::string>();
            s.name = j.value("name", s.id);
            s.description = j.value("description", std::string());
            const std::string b = j.at("behavior").get<std::string>();
            if (b == "morph") s.behavior = SkillBehavior::Morph;
            else if (b == "heal_aura") s.behavior = SkillBehavior::HealAura;
            else if (b == "stat_boost") s.behavior = SkillBehavior::StatBoost;
            else throw DataError(where + ": behavior desconocido '" + b + "' (morph, heal_aura, stat_boost)");
            s.cooldown_ticks = static_cast<uint32_t>(std::llround(j.value("cooldown", 30.0) * kTicksPerSecondData));
            s.duration_ticks = static_cast<uint32_t>(std::llround(j.value("duration", 0.0) * kTicksPerSecondData));
            s.radius = core::Fixed::from_double(j.value("radius", 4.0));
            s.heal_per_second = core::Fixed::from_double(j.value("heal_per_second", 0.0));
            for (const auto& m : j.value("modifiers", json::array())) s.modifiers.push_back(reg.parse_modifier(m, where));
            if (reg.skills_.count(s.id)) throw DataError(where + ": skill repetida '" + s.id + "'");
            reg.skills_.emplace(s.id, std::move(s));
        } catch (const json::exception& e) {
            throw DataError(where + ": " + e.what());
        }
    }

    // Las fichas solo pueden nombrar triggers y skills que existen
    for (const auto& s : reg.sheets_) {
        const std::string& where = raw.at(s.key).file;
        for (const auto& t : s.triggers) {
            if (!reg.find_trigger(t)) throw DataError(where + ": trigger '" + t + "' no existe en data/triggers");
        }
        for (const auto& k : s.skills) {
            if (!reg.find_skill(k)) throw DataError(where + ": skill '" + k + "' no existe en data/skills");
        }
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

    // Los modificadores de tecnologías, civilizaciones y habilidades solo pueden nombrar triggers y skills que existen
    for (const auto& t : reg.techs_) for (const auto& m : t.modifiers) reg.validate_references(m, "data/techs/" + t.key);
    for (const auto& c : reg.civs_) for (const auto& m : c.bonuses) reg.validate_references(m, "data/civs/" + c.key);
    for (const auto& [id, s] : reg.skills_) for (const auto& m : s.modifiers) reg.validate_references(m, "data/skills/" + id);

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
