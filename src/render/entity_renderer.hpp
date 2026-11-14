#pragma once

#include <optional>
#include <span>
#include <vector>

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/String.hpp>

#include "core/entity_handle.hpp"
#include "data/data_registry.hpp"
#include "render/camera.hpp"
#include "sim/game/game_state.hpp"

namespace render {

enum class LabelMode { Smart, All, None };

struct ViewOptions {
    LabelMode labels = LabelMode::Smart;
    bool grid = true;
    bool help = true;
};

// Una entidad dibujada este frame (también sirve para seleccionar con el ratón).
struct VisibleEntity {
    core::EntityHandle handle;
    data::TypeId type;
    data::PlayerId owner = data::kGaia;
    sf::Vector2f base;       // punto de apoyo en coordenadas iso
    sf::FloatRect hit;       // rectángulo clicable en coordenadas iso
    float label_y = 0.f;     // altura de la etiqueta (iso)
    float depth = 0.f;
    bool is_unit = false;
};

struct MoveMarker {
    sf::Vector2f iso;
    float age = 0.f;  // segundos
};

class EntityRenderer {
public:
    EntityRenderer(const sf::Font& font, const data::DataRegistry& data);

    void draw_world(sf::RenderTarget& target, const sim::GameState& game, const Camera& camera, float alpha,
                    std::span<const core::EntityHandle> selection);
    void draw_markers(sf::RenderTarget& target, std::span<const MoveMarker> markers) const;
    // Vista previa del edificio a colocar (verde = se puede, rojo = no)
    void draw_placement(sf::RenderTarget& target, const sim::GameState& game, data::TypeId type, core::TilePos tile,
                        bool valid) const;
    // Etiquetas en coordenadas de pantalla: tamaño constante con cualquier zoom.
    void draw_labels(sf::RenderWindow& window, const sim::GameState& game, const Camera& camera,
                     const ViewOptions& options, std::optional<core::EntityHandle> hovered,
                     std::span<const core::EntityHandle> selection) const;

    const std::vector<VisibleEntity>& visible() const { return visible_; }
    // Entidad visible más al frente bajo el punto (iso).
    std::optional<core::EntityHandle> pick(sf::Vector2f iso) const;

private:
    const sf::Font& font_;
    const data::DataRegistry& data_;
    std::vector<sf::String> labels_;  // por TypeId
    std::vector<VisibleEntity> visible_;
    std::vector<core::EntityHandle> query_;
};

}  // namespace render
