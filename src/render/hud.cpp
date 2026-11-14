#include "render/hud.hpp"

#include <map>
#include <string>

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include "render/text.hpp"

namespace render {

namespace {

const char* terrain_name(sim::Terrain t) {
    switch (t) {
    case sim::Terrain::Grass: return "Hierba";
    case sim::Terrain::Dirt: return "Tierra";
    case sim::Terrain::Sand: return "Arena";
    case sim::Terrain::Water: return "Agua";
    }
    return "?";
}

const char* resource_name(data::ResourceTag r) {
    switch (r) {
    case data::ResourceTag::Food: return "Comida";
    case data::ResourceTag::Wood: return "Madera";
    case data::ResourceTag::Gold: return "Oro";
    case data::ResourceTag::Stone: return "Piedra";
    }
    return "?";
}

}  // namespace

void Hud::draw(sf::RenderWindow& window, const sim::GameState& game, const HudInfo& info, const ViewOptions& options) const {
    const sf::Vector2f size(window.getSize());
    sf::Text text(font_, "", 15);
    text.setFillColor(sf::Color(240, 236, 220));

    auto put = [&](const std::string& s, sf::Vector2f pos, unsigned char_size = 15, sf::Color color = sf::Color(240, 236, 220)) {
        text.setCharacterSize(char_size);
        text.setFillColor(color);
        text.setString(utf8(s));
        text.setPosition(pos);
        window.draw(text);
        return text.getGlobalBounds();
    };

    // ─── Barra superior: recursos y población ────────────────────────────
    sf::RectangleShape top({size.x, 30.f});
    top.setFillColor(sf::Color(28, 22, 16, 225));
    window.draw(top);

    const sim::PlayerState& me = game.player(info.local_player);
    float x = 12.f;
    for (std::size_t r = 0; r < data::kResourceCount; ++r) {
        const auto tag = static_cast<data::ResourceTag>(r);
        const auto b = put(std::string(resource_name(tag)) + "  " + std::to_string(me.resources[r]), {x, 5.f});
        x = b.position.x + b.size.x + 28.f;
    }
    const bool housed = me.population_current >= me.population_cap;
    const auto pb = put("Población  " + std::to_string(me.population_current) + "/" + std::to_string(me.population_cap), {x, 5.f}, 15,
                        housed ? sf::Color(255, 140, 120) : sf::Color(240, 236, 220));
    x = pb.position.x + pb.size.x + 28.f;

    int idle = 0;
    for (core::EntityHandle h : me.units) {
        const sim::SpawnableEntity* e = game.entity(h);
        if (e && !e->action && game.sheet_of(*e).stats.has(data::Capability::Gather)) ++idle;
    }
    if (idle > 0) put("Aldeanos ociosos  " + std::to_string(idle) + "  (.)", {x, 5.f}, 15, sf::Color(250, 210, 120));

    const auto& ages = game.data().age_names();
    const std::string age = me.age < ages.size() ? ages[me.age] : std::string("?");
    const std::string right = me.name + (me.civ ? "  ·  " + me.civ->name : std::string()) + "  ·  " + age + "   ·   tick " +
                              std::to_string(game.tick()) + "  ·  " + std::to_string(static_cast<int>(info.fps)) + " FPS";
    text.setString(utf8(right));
    text.setCharacterSize(14);
    put(right, {size.x - text.getLocalBounds().size.x - 14.f, 6.f}, 14, sf::Color(200, 196, 180));

    // ─── Ayuda ───────────────────────────────────────────────────────────
    if (options.help) {
        const char* lines[] = {
            "Clic izq.: seleccionar   ·   arrastrar: selección por caja   ·   Shift: añadir",
            "Doble clic: todas las del mismo tipo en pantalla   ·   .: siguiente aldeano ocioso",
            "Clic der.: mover · recolectar (recurso) · construir (obra) · atacar (enemigo)",
            "Clic der. con un edificio seleccionado: punto de reunión",
            "Panel de órdenes: clic o tecla Q W E R T / A S D F G / Z X C V B",
            "Colocar edificio: clic izq. (Shift = varios)   ·   clic der. o Esc: cancelar",
            "Flechas / bordes / botón central: cámara   ·   Rueda: zoom   ·   Espacio: centrar",
            "L: etiquetas   ·   F2: cuadrícula   ·   H: ocultar esta ayuda   ·   Esc: deseleccionar",
        };
        sf::RectangleShape bg({600.f, 165.f});
        bg.setPosition({10.f, 38.f});
        bg.setFillColor(sf::Color(0, 0, 0, 140));
        window.draw(bg);
        float y = 44.f;
        for (const char* l : lines) {
            put(l, {18.f, y}, 13, sf::Color(220, 220, 220));
            y += 19.f;
        }
    }

    // ─── Panel de selección ──────────────────────────────────────────────
    if (!info.selection.empty()) {
        sf::RectangleShape panel({420.f, 124.f});
        panel.setPosition({10.f, size.y - 134.f});
        panel.setFillColor(sf::Color(28, 22, 16, 225));
        panel.setOutlineColor(sf::Color(120, 100, 60));
        panel.setOutlineThickness(1.f);
        window.draw(panel);
        const sf::Vector2f o = panel.getPosition() + sf::Vector2f{12.f, 8.f};

        if (info.selection.size() == 1) {
            if (const sim::SpawnableEntity* e = game.entity(info.selection.front())) {
                const auto& sheet = game.data().sheet(e->type);
                put(sheet.display_name, o, 18, sf::Color::White);
                const sim::PlayerState& owner = game.player(e->current_player);
                put("Dueño: " + owner.name + (owner.civ && e->current_player != data::kGaia ? "  (" + owner.civ->name + ")" : std::string()),
                    o + sf::Vector2f{0.f, 26.f}, 14);
                // Stats efectivos (con bonus de civ y tecnologías del jugador original)
                const auto& st = game.stats_of(*e);
                std::string line1, line2, line3;
                if (st.max_hp > 1) line1 = "Vida: " + std::to_string(e->hp) + " / " + std::to_string(st.max_hp);
                if (sheet.can_attack() && !sheet.is_resource()) {
                    std::string atk;
                    for (const auto& a : st.attacks) {
                        if (a.value == 0) continue;
                        if (!atk.empty()) atk += " + ";
                        atk += std::to_string(a.value) + (a.armor_class == 0 ? "" : a.armor_class == 1 ? " a dist." : " vs " + game.data().armor_class_names()[a.armor_class]);
                    }
                    line3 = "Ataque " + atk + "   ·   Armadura " + std::to_string(st.armor_vs(0)) + "/" + std::to_string(st.armor_vs(1));
                    if (st.range > core::Fixed{}) line3 += "   ·   Alcance " + std::to_string(st.range.floor_int());
                } else if (!sheet.is_resource() && st.max_hp > 1) {
                    line3 = "Armadura " + std::to_string(st.armor_vs(0)) + "/" + std::to_string(st.armor_vs(1));
                }
                if (e->resource_node) {
                    line1 = std::string(resource_name(e->resource_node->resource)) + ": " + std::to_string(e->resource_node->amount_remaining);
                }
                if (sheet.is_unit()) {
                    line2 = e->action ? e->action->label() : "Inactivo";
                    if (e->carry.amount > core::Fixed{}) {
                        line2 += "   ·   lleva " + std::to_string(e->carry.amount.floor_int()) + " " + resource_name(e->carry.resource);
                    }
                } else if (e->construction) {
                    const int pct = static_cast<int>(100.f * e->construction->progress.to_float() /
                                                     static_cast<float>(std::max<uint32_t>(1, e->construction->total_ticks)));
                    line2 = "En obra: " + std::to_string(pct) + "%  (clic der. con aldeanos para ayudar)";
                } else if (!e->production.empty()) {
                    const auto& job = e->production.front();
                    const bool blocked = !job.tech && job.ticks_remaining == 0 &&
                                         owner.population_current + game.data().sheet(job.type).stats.population_cost > owner.population_cap;
                    line2 = (job.tech ? "Investigando " + game.data().tech(*job.tech).name : "Entrenando " + game.data().sheet(job.type).display_name) +
                            (blocked ? "  ·  falta población: construye casas"
                                     : "  ·  " + std::to_string((job.ticks_remaining + sim::kTicksPerSecond - 1) / sim::kTicksPerSecond) + " s");
                    if (e->production.size() > 1) line2 += "  (+" + std::to_string(e->production.size() - 1) + " en cola)";
                } else if (sheet.stats.dropoff.any()) {
                    line2 = "Depósito de";
                    for (std::size_t r = 0; r < data::kResourceCount; ++r) {
                        if (sheet.stats.dropoff.test(r)) line2 += std::string(" ") + resource_name(static_cast<data::ResourceTag>(r));
                    }
                }
                if (!line1.empty()) put(line1, o + sf::Vector2f{0.f, 46.f}, 14);
                if (!line3.empty()) put(line3, o + sf::Vector2f{0.f, 66.f}, 14, sf::Color(200, 210, 230));
                if (!line2.empty()) put(line2, o + sf::Vector2f{0.f, line3.empty() ? 66.f : 86.f}, 14, sf::Color(250, 220, 150));
            }
        } else {
            std::map<std::string, int> counts;
            for (core::EntityHandle h : info.selection) {
                if (const sim::SpawnableEntity* e = game.entity(h)) ++counts[game.data().sheet(e->type).display_name];
            }
            put(std::to_string(info.selection.size()) + " seleccionados", o, 18, sf::Color::White);
            float y = o.y + 28.f;
            for (const auto& [name, n] : counts) {
                put(name + "  ×" + std::to_string(n), {o.x, y}, 14);
                y += 20.f;
                if (y > size.y - 20.f) break;
            }
        }
    }

    // ─── Casilla bajo el ratón ───────────────────────────────────────────
    if (info.hovered_tile && game.map().in_bounds(*info.hovered_tile)) {
        const core::TilePos t = *info.hovered_tile;
        const std::string s = std::string("Casilla ") + std::to_string(t.x) + ", " + std::to_string(t.y) + "  ·  " +
                              terrain_name(game.map().terrain(t.x, t.y)) + (game.map().is_passable(t) ? "" : "  ·  bloqueada");
        text.setCharacterSize(13);
        text.setString(utf8(s));
        put(s, {size.x - text.getLocalBounds().size.x - 14.f, size.y - 24.f}, 13, sf::Color(220, 220, 220));
    }

    // ─── Caja de selección ───────────────────────────────────────────────
    if (info.drag_rect) {
        sf::RectangleShape box(info.drag_rect->size);
        box.setPosition(info.drag_rect->position);
        box.setFillColor(sf::Color(120, 200, 255, 40));
        box.setOutlineColor(sf::Color(160, 220, 255));
        box.setOutlineThickness(1.f);
        window.draw(box);
    }
}

}  // namespace render
