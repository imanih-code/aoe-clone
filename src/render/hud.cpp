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
    put("Población  " + std::to_string(me.population_current) + "/" + std::to_string(me.population_cap), {x, 5.f});

    const std::string right = me.name + "   ·   tick " + std::to_string(game.tick()) + "   ·   " + std::to_string(static_cast<int>(info.fps)) + " FPS";
    text.setString(utf8(right));
    text.setCharacterSize(14);
    put(right, {size.x - text.getLocalBounds().size.x - 14.f, 6.f}, 14, sf::Color(200, 196, 180));

    // ─── Ayuda ───────────────────────────────────────────────────────────
    if (options.help) {
        const char* lines[] = {
            "Clic izq.: seleccionar   ·   arrastrar: selección por caja   ·   Shift: añadir",
            "Doble clic: todas las del mismo tipo en pantalla",
            "Clic der.: mover   ·   S: detener   ·   Espacio: centrar en la selección",
            "Flechas / bordes / botón central: mover cámara   ·   Rueda: zoom",
            "L: etiquetas   ·   G: cuadrícula   ·   H: ocultar esta ayuda   ·   Esc: deseleccionar",
        };
        sf::RectangleShape bg({560.f, 108.f});
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
        sf::RectangleShape panel({380.f, 104.f});
        panel.setPosition({10.f, size.y - 114.f});
        panel.setFillColor(sf::Color(28, 22, 16, 225));
        panel.setOutlineColor(sf::Color(120, 100, 60));
        panel.setOutlineThickness(1.f);
        window.draw(panel);
        const sf::Vector2f o = panel.getPosition() + sf::Vector2f{12.f, 8.f};

        if (info.selection.size() == 1) {
            if (const sim::SpawnableEntity* e = game.entity(info.selection.front())) {
                const auto& sheet = game.data().sheet(e->type);
                put(sheet.display_name, o, 18, sf::Color::White);
                put("Dueño: " + game.player(e->current_player).name, o + sf::Vector2f{0.f, 28.f}, 14);
                if (sheet.stats.max_hp > 1) {
                    put("Vida: " + std::to_string(e->hp) + " / " + std::to_string(sheet.stats.max_hp), o + sf::Vector2f{0.f, 48.f}, 14);
                }
                if (e->resource_node) {
                    put(std::string(resource_name(e->resource_node->resource)) + ": " + std::to_string(e->resource_node->amount_remaining),
                        o + sf::Vector2f{0.f, 48.f}, 14);
                }
                const core::TilePos t = core::tile_of(e->position);
                put("Casilla: " + std::to_string(t.x) + ", " + std::to_string(t.y), o + sf::Vector2f{0.f, 68.f}, 14);
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
