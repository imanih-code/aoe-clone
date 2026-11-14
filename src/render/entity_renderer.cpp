#include "render/entity_renderer.hpp"

#include <algorithm>
#include <cmath>

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/ConvexShape.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include "render/iso.hpp"
#include "render/text.hpp"

namespace render {

namespace {

const sf::Color kOutline(20, 20, 20, 200);
const sf::Color kSelected(255, 255, 255);

bool contains(std::span<const core::EntityHandle> v, core::EntityHandle h) {
    return std::find(v.begin(), v.end(), h) != v.end();
}

sf::CircleShape ellipse(float r, sf::Vector2f center, float squash) {
    sf::CircleShape c(r, 24);
    c.setOrigin({r, r});
    c.setScale({1.f, squash});
    c.setPosition(center);
    return c;
}

sf::ConvexShape quad(sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Vector2f d, sf::Color fill) {
    sf::ConvexShape q(4);
    q.setPoint(0, a);
    q.setPoint(1, b);
    q.setPoint(2, c);
    q.setPoint(3, d);
    q.setFillColor(fill);
    q.setOutlineColor(kOutline);
    q.setOutlineThickness(1.f);
    return q;
}

sf::FloatRect bounds_of(std::initializer_list<sf::Vector2f> pts) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (auto p : pts) {
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x);
        y1 = std::max(y1, p.y);
    }
    return sf::FloatRect({x0, y0}, {x1 - x0, y1 - y0});
}

sf::Color resource_color(data::ResourceTag r) {
    switch (r) {
    case data::ResourceTag::Food: return {214, 70, 110};
    case data::ResourceTag::Wood: return {150, 98, 50};
    case data::ResourceTag::Gold: return {236, 196, 64};
    case data::ResourceTag::Stone: return {170, 170, 170};
    }
    return sf::Color::White;
}

sf::Color owner_color(const sim::GameState& game, const data::VisualDef& v, data::PlayerId owner) {
    if (v.use_player_color && owner != data::kGaia) return rgba(game.player(owner).color);
    return rgba(v.color);
}

}  // namespace

EntityRenderer::EntityRenderer(const sf::Font& font, const data::DataRegistry& data) : font_(font), data_(data) {
    for (const auto& sheet : data.sheets()) labels_.push_back(utf8(sheet.visual.label));
}

void EntityRenderer::draw_world(sf::RenderTarget& target, const sim::GameState& game, const Camera& camera, float alpha,
                                std::span<const core::EntityHandle> selection, std::optional<data::PlayerId> viewer) {
    visible_.clear();
    query_.clear();

    // Rectángulo de tiles que cubre la vista (+ margen para edificios altos)
    const sf::FloatRect view = camera.visible_rect();
    const sf::Vector2f corners[4] = {iso_to_world(view.position), iso_to_world(view.position + sf::Vector2f{view.size.x, 0.f}),
                                     iso_to_world(view.position + sf::Vector2f{0.f, view.size.y}), iso_to_world(view.position + view.size)};
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (auto c : corners) {
        x0 = std::min(x0, c.x);
        y0 = std::min(y0, c.y);
        x1 = std::max(x1, c.x);
        y1 = std::max(y1, c.y);
    }
    game.spatial().query_rect({core::Fixed::from_double(x0 - 4.0), core::Fixed::from_double(y0 - 4.0)},
                              {core::Fixed::from_double(x1 + 4.0), core::Fixed::from_double(y1 + 4.0)}, query_);

    const sf::FloatRect padded(view.position - sf::Vector2f{120.f, 160.f}, view.size + sf::Vector2f{240.f, 320.f});

    for (core::EntityHandle h : query_) {
        const sim::SpawnableEntity* e = game.entity(h);
        if (!e) continue;
        const auto& sheet = data_.sheet(e->type);
        if (viewer) {
            // Niebla: unidades y flechas solo si se ven ahora; edificios y recursos si se exploraron
            const bool shown = (sheet.is_unit() || sheet.is_projectile()) ? game.is_visible_to(*viewer, *e) : game.is_known_to(*viewer, *e);
            if (!shown) continue;
        }
        const float wx = e->prev_position.x.to_float() + (e->position.x.to_float() - e->prev_position.x.to_float()) * alpha;
        const float wy = e->prev_position.y.to_float() + (e->position.y.to_float() - e->prev_position.y.to_float()) * alpha;
        const sf::Vector2f base = world_to_iso(wx, wy);
        if (!padded.contains(base)) continue;

        VisibleEntity v;
        v.handle = h;
        v.type = e->type;
        v.owner = e->current_player;
        v.base = base;
        // Lo que está a ras de suelo (campos, carne) se dibuja antes que todo lo demás
        const bool flat = sheet.visual.shape == data::ShapeKind::Field || sheet.visual.shape == data::ShapeKind::Carcass;
        v.depth = (flat ? -1.0e6f : 0.f) + wx + wy;
        v.is_unit = sheet.is_unit();
        visible_.push_back(v);
    }

    std::sort(visible_.begin(), visible_.end(), [](const VisibleEntity& a, const VisibleEntity& b) {
        if (a.depth != b.depth) return a.depth < b.depth;
        return a.handle.index < b.handle.index;
    });

    for (VisibleEntity& v : visible_) {
        const sim::SpawnableEntity& e = *game.entity(v.handle);
        const auto& sheet = data_.sheet(e.type);
        const auto& vis = sheet.visual;
        const sf::Color color = owner_color(game, vis, v.owner);
        const bool selected = contains(selection, v.handle);
        const sf::Vector2f p = v.base;

        switch (vis.shape) {
        case data::ShapeKind::Unit: {
            const float r = vis.size * 40.f;
            auto shadow = ellipse(r * 1.1f, p, 0.5f);
            shadow.setFillColor({0, 0, 0, 70});
            target.draw(shadow);
            if (selected) {
                auto ring = ellipse(r * 1.6f, p, 0.5f);
                ring.setFillColor(sf::Color::Transparent);
                ring.setOutlineColor(kSelected);
                ring.setOutlineThickness(1.5f);
                target.draw(ring);
            }
            sf::CircleShape body(r, 20);
            body.setOrigin({r, r});
            body.setPosition(p - sf::Vector2f{0.f, r + 2.f});
            body.setFillColor(color);
            body.setOutlineColor(shade(color, 0.45f));
            body.setOutlineThickness(1.5f);
            target.draw(body);
            sf::CircleShape head(r * 0.38f, 12);
            head.setOrigin({r * 0.38f, r * 0.38f});
            head.setPosition(p - sf::Vector2f{0.f, r * 1.25f + 2.f});
            head.setFillColor(sf::Color(236, 206, 170));
            target.draw(head);
            // Carga que lleva (cuadradito del color del recurso)
            if (e.carry.amount > core::Fixed{}) {
                sf::RectangleShape load({r * 0.8f, r * 0.8f});
                load.setOrigin({r * 0.4f, r * 0.4f});
                load.setPosition(p + sf::Vector2f{r * 0.9f, -r * 0.9f});
                load.setFillColor(resource_color(e.carry.resource));
                load.setOutlineColor(kOutline);
                load.setOutlineThickness(1.f);
                target.draw(load);
            }
            v.hit = sf::FloatRect({p.x - r * 1.3f, p.y - r * 2.4f}, {r * 2.6f, r * 2.8f});
            v.label_y = p.y - r * 2.2f - 4.f;
            break;
        }
        case data::ShapeKind::Cavalry: {
            const float r = vis.size * 40.f;
            auto shadow = ellipse(r * 1.5f, p, 0.45f);
            shadow.setFillColor({0, 0, 0, 70});
            target.draw(shadow);
            if (selected) {
                auto ring = ellipse(r * 1.9f, p, 0.5f);
                ring.setFillColor(sf::Color::Transparent);
                ring.setOutlineColor(kSelected);
                ring.setOutlineThickness(1.5f);
                target.draw(ring);
            }
            auto horse = ellipse(r, p - sf::Vector2f{0.f, r * 0.75f}, 0.6f);
            horse.setScale({1.45f, 0.6f});
            horse.setFillColor(sf::Color(120, 84, 52));
            horse.setOutlineColor(kOutline);
            horse.setOutlineThickness(1.f);
            target.draw(horse);
            const float rr = r * 0.6f;
            sf::CircleShape rider(rr, 16);
            rider.setOrigin({rr, rr});
            rider.setPosition(p - sf::Vector2f{0.f, r * 1.55f});
            rider.setFillColor(color);
            rider.setOutlineColor(shade(color, 0.45f));
            rider.setOutlineThickness(1.5f);
            target.draw(rider);
            v.hit = sf::FloatRect({p.x - r * 1.6f, p.y - r * 2.4f}, {r * 3.2f, r * 2.8f});
            v.label_y = p.y - r * 2.3f - 4.f;
            break;
        }
        case data::ShapeKind::Building: {
            const sf::Color base_color = color;
            const float ax = static_cast<float>(e.anchor.x);
            const float ay = static_cast<float>(e.anchor.y);
            const float fw = sheet.stats.footprint_w;
            const float fh = sheet.stats.footprint_h;
            // En obra: más bajo y translúcido según el progreso
            float progress = 1.f;
            if (e.construction) {
                progress = e.construction->progress.to_float() / static_cast<float>(std::max<uint32_t>(1, e.construction->total_ticks));
            }
            const float hpx = vis.height * kTileH * (0.15f + 0.85f * progress);
            const sf::Vector2f up{0.f, -hpx};
            const sf::Color color = e.construction ? sf::Color(base_color.r, base_color.g, base_color.b, 150) : base_color;
            const sf::Vector2f T = world_to_iso(ax, ay), R = world_to_iso(ax + fw, ay);
            const sf::Vector2f B = world_to_iso(ax + fw, ay + fh), L = world_to_iso(ax, ay + fh);
            // Margen interior para que se vea el suelo alrededor
            const sf::Vector2f c = (T + B) * 0.5f;
            auto inset = [&c](sf::Vector2f q) { return c + (q - c) * 0.86f; };
            const sf::Vector2f t = inset(T), r = inset(R), b = inset(B), l = inset(L);

            target.draw(quad(T, R, B, L, sf::Color(0, 0, 0, 60)));
            if (selected) {
                auto outline = quad(T, R, B, L, sf::Color::Transparent);
                outline.setOutlineColor(kSelected);
                outline.setOutlineThickness(1.5f);
                target.draw(outline);
            }
            target.draw(quad(l, b, b + up, l + up, shade(color, 0.62f)));  // cara izquierda
            target.draw(quad(b, r, r + up, b + up, shade(color, 0.8f)));   // cara derecha
            auto roof = quad(t + up, r + up, b + up, l + up, mix(color, sf::Color::White, 0.18f));
            if (selected) roof.setOutlineColor(kSelected);
            target.draw(roof);
            v.hit = bounds_of({t + up, r + up, b, l + up, r, l});
            v.label_y = t.y + up.y + (b.y - t.y) * 0.5f;
            v.depth = (ax + fw * 0.5f) + (ay + fh * 0.5f);
            break;
        }
        case data::ShapeKind::Tree: {
            const float r = vis.size * 40.f;
            const float h = vis.height * 24.f;
            auto shadow = ellipse(r, p, 0.45f);
            shadow.setFillColor({0, 0, 0, 60});
            target.draw(shadow);
            sf::RectangleShape trunk({4.f, h * 0.35f});
            trunk.setOrigin({2.f, h * 0.35f});
            trunk.setPosition(p);
            trunk.setFillColor(sf::Color(96, 64, 36));
            target.draw(trunk);
            for (int i = 0; i < 2; ++i) {
                const float rr = r * (i == 0 ? 1.f : 0.75f);
                sf::CircleShape crown(rr, 3);
                crown.setOrigin({rr, rr});
                crown.setPosition(p - sf::Vector2f{0.f, h * (0.55f + 0.3f * static_cast<float>(i))});
                crown.setFillColor(i == 0 ? shade(color, 0.85f) : color);
                crown.setOutlineColor(shade(color, 0.5f));
                crown.setOutlineThickness(1.f);
                target.draw(crown);
            }
            v.hit = sf::FloatRect({p.x - r, p.y - h * 0.85f - r}, {r * 2.f, h * 0.85f + r});
            v.label_y = p.y - h * 0.85f - r - 2.f;
            break;
        }
        case data::ShapeKind::Mine: {
            const float r = vis.size * 40.f;
            sf::ConvexShape rock(7);
            const float pts[7][2] = {{-1.f, 0.f}, {-0.75f, -0.7f}, {-0.2f, -1.05f}, {0.45f, -0.9f}, {1.f, -0.35f}, {0.85f, 0.25f}, {0.f, 0.35f}};
            for (std::size_t i = 0; i < 7; ++i) rock.setPoint(i, p + sf::Vector2f{pts[i][0] * r, pts[i][1] * r});
            rock.setFillColor(color);
            rock.setOutlineColor(shade(color, 0.45f));
            rock.setOutlineThickness(1.5f);
            target.draw(rock);
            sf::CircleShape shine(r * 0.18f, 8);
            shine.setOrigin({r * 0.18f, r * 0.18f});
            shine.setPosition(p + sf::Vector2f{-r * 0.25f, -r * 0.6f});
            shine.setFillColor(mix(color, sf::Color::White, 0.55f));
            target.draw(shine);
            if (selected) {
                auto ring = ellipse(r * 1.3f, p, 0.5f);
                ring.setFillColor(sf::Color::Transparent);
                ring.setOutlineColor(kSelected);
                ring.setOutlineThickness(1.5f);
                target.draw(ring);
            }
            v.hit = sf::FloatRect({p.x - r, p.y - r * 1.1f}, {r * 2.f, r * 1.5f});
            v.label_y = p.y - r * 1.1f - 2.f;
            break;
        }
        case data::ShapeKind::Bush: {
            const float r = vis.size * 40.f;
            const sf::Color leaf(58, 112, 52);
            const float offs[3][2] = {{-0.45f, -0.3f}, {0.45f, -0.3f}, {0.f, -0.65f}};
            for (const auto& o : offs) {
                sf::CircleShape c(r * 0.55f, 14);
                c.setOrigin({r * 0.55f, r * 0.55f});
                c.setPosition(p + sf::Vector2f{o[0] * r, o[1] * r});
                c.setFillColor(leaf);
                c.setOutlineColor(shade(leaf, 0.5f));
                c.setOutlineThickness(1.f);
                target.draw(c);
            }
            const float berries[5][2] = {{-0.5f, -0.35f}, {0.35f, -0.45f}, {0.05f, -0.8f}, {0.55f, -0.15f}, {-0.15f, -0.2f}};
            for (const auto& b : berries) {
                sf::CircleShape berry(r * 0.13f, 8);
                berry.setOrigin({r * 0.13f, r * 0.13f});
                berry.setPosition(p + sf::Vector2f{b[0] * r, b[1] * r});
                berry.setFillColor(color);
                target.draw(berry);
            }
            v.hit = sf::FloatRect({p.x - r, p.y - r * 1.2f}, {r * 2.f, r * 1.4f});
            v.label_y = p.y - r * 1.25f - 2.f;
            break;
        }
        case data::ShapeKind::Arrow: {
            // Trazo en el sentido del vuelo, elevado del suelo; no se puede seleccionar
            sf::Vector2f dir = world_to_iso(e.position) - world_to_iso(e.prev_position);
            const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            dir = len > 0.01f ? dir / len : sf::Vector2f{1.f, 0.f};
            const sf::Vector2f c = p - sf::Vector2f{0.f, 14.f};
            const sf::Vertex shaft[] = {{c - dir * 7.f, sf::Color(60, 40, 20)}, {c + dir * 7.f, sf::Color(60, 40, 20)}};
            target.draw(shaft, 2, sf::PrimitiveType::Lines);
            const sf::Vertex tip[] = {{c + dir * 4.f, sf::Color(230, 230, 230)}, {c + dir * 7.f, sf::Color(230, 230, 230)}};
            target.draw(tip, 2, sf::PrimitiveType::Lines);
            v.hit = sf::FloatRect(p, {0.f, 0.f});
            v.label_y = p.y;
            break;
        }
        case data::ShapeKind::Animal: {
            // Cuadrúpedo: cuerpo, cabeza mirando hacia donde camina y cuatro patas
            const float r = vis.size * 40.f;
            const sf::Color body_color = rgba(vis.color);
            sf::Vector2f dir = world_to_iso(e.position) - world_to_iso(e.prev_position);
            const float facing = dir.x < -0.01f ? -1.f : 1.f;
            auto shadow = ellipse(r * 1.3f, p, 0.4f);
            shadow.setFillColor({0, 0, 0, 60});
            target.draw(shadow);
            if (selected || v.owner != data::kGaia) {
                // Ganado con dueño: collar del color del jugador
                auto ring = ellipse(r * 1.5f, p, 0.45f);
                ring.setFillColor(sf::Color::Transparent);
                ring.setOutlineColor(selected ? kSelected : rgba(game.player(v.owner).color));
                ring.setOutlineThickness(1.5f);
                target.draw(ring);
            }
            const sf::Color leg = shade(body_color, 0.55f);
            for (float lx : {-0.6f, -0.25f, 0.25f, 0.6f}) {
                const sf::Vertex l[] = {{p + sf::Vector2f{lx * r, -r * 0.5f}, leg}, {p + sf::Vector2f{lx * r, 0.f}, leg}};
                target.draw(l, 2, sf::PrimitiveType::Lines);
            }
            auto torso = ellipse(r, p - sf::Vector2f{0.f, r * 0.8f}, 0.6f);
            torso.setScale({1.25f, 0.65f});
            torso.setFillColor(body_color);
            torso.setOutlineColor(shade(body_color, 0.5f));
            torso.setOutlineThickness(1.f);
            target.draw(torso);
            sf::CircleShape head(r * 0.42f, 12);
            head.setOrigin({r * 0.42f, r * 0.42f});
            head.setPosition(p + sf::Vector2f{facing * r * 1.15f, -r * 1.15f});
            head.setFillColor(body_color);
            head.setOutlineColor(shade(body_color, 0.5f));
            head.setOutlineThickness(1.f);
            target.draw(head);
            v.hit = sf::FloatRect({p.x - r * 1.6f, p.y - r * 1.8f}, {r * 3.2f, r * 1.9f});
            v.label_y = p.y - r * 1.7f - 2.f;
            break;
        }
        case data::ShapeKind::Carcass: {
            const float r = vis.size * 40.f;
            auto meat = ellipse(r, p, 0.45f);
            meat.setFillColor(color);
            meat.setOutlineColor(shade(color, 0.5f));
            meat.setOutlineThickness(1.f);
            target.draw(meat);
            auto bone = ellipse(r * 0.35f, p - sf::Vector2f{r * 0.2f, r * 0.1f}, 0.5f);
            bone.setFillColor(sf::Color(235, 225, 205));
            target.draw(bone);
            v.hit = sf::FloatRect({p.x - r, p.y - r * 0.6f}, {r * 2.f, r * 1.2f});
            v.label_y = p.y - r * 0.6f - 2.f;
            break;
        }
        case data::ShapeKind::Fish: {
            // Ondas en el agua y unos peces
            const float r = vis.size * 40.f;
            auto ripple = ellipse(r * 1.1f, p, 0.5f);
            ripple.setFillColor(sf::Color(255, 255, 255, 30));
            ripple.setOutlineColor(sf::Color(255, 255, 255, 120));
            ripple.setOutlineThickness(1.f);
            target.draw(ripple);
            const float offs[3][2] = {{-0.45f, -0.05f}, {0.35f, -0.2f}, {0.05f, 0.18f}};
            for (const auto& o : offs) {
                auto fish = ellipse(r * 0.28f, p + sf::Vector2f{o[0] * r, o[1] * r}, 0.45f);
                fish.setFillColor(color);
                fish.setOutlineColor(shade(color, 0.6f));
                fish.setOutlineThickness(1.f);
                target.draw(fish);
            }
            v.hit = sf::FloatRect({p.x - r, p.y - r * 0.6f}, {r * 2.f, r * 1.2f});
            v.label_y = p.y - r * 0.6f - 2.f;
            break;
        }
        case data::ShapeKind::Boat: {
            const float r = vis.size * 40.f;
            if (selected) {
                auto ring = ellipse(r * 1.6f, p, 0.5f);
                ring.setFillColor(sf::Color::Transparent);
                ring.setOutlineColor(kSelected);
                ring.setOutlineThickness(1.5f);
                target.draw(ring);
            }
            sf::ConvexShape hull(4);
            hull.setPoint(0, p + sf::Vector2f{-r * 1.3f, -r * 0.45f});
            hull.setPoint(1, p + sf::Vector2f{r * 1.3f, -r * 0.45f});
            hull.setPoint(2, p + sf::Vector2f{r * 0.9f, r * 0.15f});
            hull.setPoint(3, p + sf::Vector2f{-r * 0.9f, r * 0.15f});
            hull.setFillColor(sf::Color(118, 82, 48));
            hull.setOutlineColor(kOutline);
            hull.setOutlineThickness(1.f);
            target.draw(hull);
            const sf::Vertex mast[] = {{p + sf::Vector2f{0.f, -r * 0.45f}, sf::Color(70, 50, 30)},
                                       {p + sf::Vector2f{0.f, -r * 2.2f}, sf::Color(70, 50, 30)}};
            target.draw(mast, 2, sf::PrimitiveType::Lines);
            sf::ConvexShape sail(3);
            sail.setPoint(0, p + sf::Vector2f{1.f, -r * 2.1f});
            sail.setPoint(1, p + sf::Vector2f{r * 1.0f, -r * 0.7f});
            sail.setPoint(2, p + sf::Vector2f{1.f, -r * 0.6f});
            sail.setFillColor(color);
            sail.setOutlineColor(shade(color, 0.5f));
            sail.setOutlineThickness(1.f);
            target.draw(sail);
            if (e.carry.amount > core::Fixed{}) {
                auto load = ellipse(r * 0.25f, p + sf::Vector2f{-r * 0.6f, -r * 0.55f}, 0.6f);
                load.setFillColor(resource_color(e.carry.resource));
                target.draw(load);
            }
            v.hit = sf::FloatRect({p.x - r * 1.4f, p.y - r * 2.3f}, {r * 2.8f, r * 2.6f});
            v.label_y = p.y - r * 2.3f - 3.f;
            break;
        }
        case data::ShapeKind::Field: {
            // Campo plano con surcos; amarillea según se agota
            const float ax = static_cast<float>(e.anchor.x), ay = static_cast<float>(e.anchor.y);
            const float fw = sheet.stats.footprint_w, fh = sheet.stats.footprint_h;
            float ripe = 1.f;
            if (e.resource_node && e.resource_node->amount_max > 0) {
                ripe = static_cast<float>(e.resource_node->amount_remaining) / static_cast<float>(e.resource_node->amount_max);
            }
            sf::Color soil = mix(sf::Color(170, 140, 80), sf::Color(120, 150, 60), ripe);
            if (e.construction) soil = sf::Color(150, 120, 80, 150);
            auto field = quad(world_to_iso(ax, ay), world_to_iso(ax + fw, ay), world_to_iso(ax + fw, ay + fh), world_to_iso(ax, ay + fh), soil);
            if (selected) {
                field.setOutlineColor(kSelected);
                field.setOutlineThickness(1.5f);
            }
            target.draw(field);
            sf::VertexArray furrows(sf::PrimitiveType::Lines);
            const sf::Color line = shade(soil, 0.75f);
            for (int i = 1; i < 6; ++i) {
                const float t = static_cast<float>(i) / 6.f;
                furrows.append({world_to_iso(ax + fw * t, ay + 0.1f), line});
                furrows.append({world_to_iso(ax + fw * t, ay + fh - 0.1f), line});
            }
            target.draw(furrows);
            v.hit = bounds_of({world_to_iso(ax, ay), world_to_iso(ax + fw, ay), world_to_iso(ax + fw, ay + fh), world_to_iso(ax, ay + fh)});
            v.label_y = world_to_iso(ax, ay).y + 6.f;
            break;
        }
        }

        // Barra de vida: lo seleccionado y todo lo que esté herido (unidades y edificios)
        const int32_t max_hp = sheet.is_projectile() || sheet.is_resource() ? 0 : game.stats_of(e).max_hp;
        if (max_hp > 1 && (selected || (e.hp < max_hp && !e.construction))) {
            const float w = sheet.is_building() ? 48.f : 30.f;
            const float ratio = std::clamp(static_cast<float>(e.hp) / static_cast<float>(max_hp), 0.f, 1.f);
            sf::RectangleShape bg({w, 4.f});
            bg.setPosition({p.x - w * 0.5f, v.hit.position.y - 7.f});
            bg.setFillColor(sf::Color(40, 0, 0, 200));
            target.draw(bg);
            sf::RectangleShape fg({w * ratio, 4.f});
            fg.setPosition(bg.getPosition());
            fg.setFillColor(ratio > 0.5f ? sf::Color(70, 220, 70) : ratio > 0.25f ? sf::Color(230, 200, 60) : sf::Color(230, 70, 60));
            target.draw(fg);
        }

        // Órdenes en cola (Shift) de lo seleccionado: la ruta que va a seguir, en orden
        if (selected && !e.orders.empty()) {
            sf::VertexArray route(sf::PrimitiveType::LineStrip);
            route.append({p, sf::Color(255, 230, 120, 90)});
            for (const auto& order : e.orders) {
                const sf::Vector2f q = world_to_iso(order.pos);
                route.append({q, sf::Color(255, 230, 120, 160)});
                sf::CircleShape dot(3.f, 8);
                dot.setOrigin({3.f, 3.f});
                dot.setPosition(q);
                dot.setFillColor(sf::Color(255, 230, 120, 200));
                target.draw(dot);
            }
            target.draw(route);
        }

        // Punto de reunión del edificio seleccionado
        if (selected && e.rally_point) {
            const sf::Vector2f flag = world_to_iso(*e.rally_point);
            const sf::Vertex line[] = {{p, sf::Color(255, 255, 255, 110)}, {flag, sf::Color(255, 255, 255, 110)}};
            target.draw(line, 2, sf::PrimitiveType::Lines);
            sf::RectangleShape pole({2.f, 20.f});
            pole.setOrigin({1.f, 20.f});
            pole.setPosition(flag);
            pole.setFillColor(sf::Color(60, 40, 20));
            target.draw(pole);
            sf::ConvexShape banner(3);
            banner.setPoint(0, flag + sf::Vector2f{1.f, -20.f});
            banner.setPoint(1, flag + sf::Vector2f{14.f, -15.f});
            banner.setPoint(2, flag + sf::Vector2f{1.f, -10.f});
            banner.setFillColor(color);
            target.draw(banner);
        }
    }
}

void EntityRenderer::draw_placement(sf::RenderTarget& target, const sim::GameState& game, data::TypeId type, core::TilePos tile,
                                    bool valid) const {
    const auto& s = game.data().sheet(type);
    const float ax = static_cast<float>(tile.x), ay = static_cast<float>(tile.y);
    const float fw = s.stats.footprint_w, fh = s.stats.footprint_h;
    const sf::Color fill = valid ? sf::Color(80, 220, 80, 90) : sf::Color(230, 60, 60, 110);
    const sf::Color edge = valid ? sf::Color(140, 255, 140) : sf::Color(255, 120, 120);
    auto ghost = quad(world_to_iso(ax, ay), world_to_iso(ax + fw, ay), world_to_iso(ax + fw, ay + fh), world_to_iso(ax, ay + fh), fill);
    ghost.setOutlineColor(edge);
    ghost.setOutlineThickness(1.5f);
    target.draw(ghost);
    // Silueta del edificio
    const float hpx = s.visual.height * kTileH;
    const sf::Vector2f up{0.f, -hpx};
    const sf::Vector2f c = world_to_iso(ax + fw * 0.5f, ay + fh * 0.5f);
    auto roof = quad(world_to_iso(ax, ay) + up, world_to_iso(ax + fw, ay) + up, world_to_iso(ax + fw, ay + fh) + up,
                     world_to_iso(ax, ay + fh) + up, sf::Color(255, 255, 255, 40));
    roof.setOutlineColor(edge);
    target.draw(roof);
    const sf::Vertex mast[] = {{c, edge}, {c + up, edge}};
    target.draw(mast, 2, sf::PrimitiveType::Lines);
}

void EntityRenderer::draw_markers(sf::RenderTarget& target, std::span<const MoveMarker> markers) const {
    for (const MoveMarker& m : markers) {
        const float t = std::clamp(m.age / 0.6f, 0.f, 1.f);
        auto ring = ellipse(6.f + 10.f * t, m.iso, 0.5f);
        ring.setFillColor(sf::Color::Transparent);
        ring.setOutlineColor(sf::Color(120, 255, 120, static_cast<std::uint8_t>(255.f * (1.f - t))));
        ring.setOutlineThickness(2.f);
        target.draw(ring);
    }
}

void EntityRenderer::draw_labels(sf::RenderWindow& window, const sim::GameState& game, const Camera& camera,
                                 const ViewOptions& options, std::optional<core::EntityHandle> hovered,
                                 std::span<const core::EntityHandle> selection) const {
    if (options.labels == LabelMode::None && !hovered && selection.empty()) return;
    const unsigned size = 13;
    sf::Text text(font_, "", size);
    text.setOutlineColor(sf::Color(0, 0, 0, 220));
    text.setOutlineThickness(2.f);

    for (const VisibleEntity& v : visible_) {
        const auto& sheet = data_.sheet(v.type);
        if (sheet.visual.label.empty() || sheet.is_projectile()) continue;  // flechas: sin etiqueta
        const bool is_hovered = hovered && *hovered == v.handle;
        const bool is_selected = contains(selection, v.handle);
        bool show = is_hovered || is_selected;
        if (!show) {
            switch (options.labels) {
            case LabelMode::All: show = true; break;
            case LabelMode::None: show = false; break;
            case LabelMode::Smart:
                // Árboles solo con zoom cercano: si no, el bosque es un muro de texto
                show = sheet.visual.shape != data::ShapeKind::Tree || camera.zoom() <= 0.7f;
                break;
            }
        }
        if (!show) continue;

        const sim::SpawnableEntity* ent = game.entity(v.handle);
        if (ent && ent->construction) {
            const int pct = static_cast<int>(100.f * ent->construction->progress.to_float() /
                                             static_cast<float>(std::max<uint32_t>(1, ent->construction->total_ticks)));
            text.setString(labels_[v.type.value] + utf8(" · en obra " + std::to_string(pct) + "%"));
        } else {
            text.setString(labels_[v.type.value]);
        }
        sf::Color fill = sf::Color(235, 235, 235);
        if (v.owner != data::kGaia) fill = mix(rgba(game.player(v.owner).color), sf::Color::White, 0.55f);
        if (is_hovered || is_selected) fill = sf::Color::White;
        text.setFillColor(fill);
        text.setStyle(is_hovered || is_selected ? sf::Text::Bold : sf::Text::Regular);

        const sf::Vector2i px = window.mapCoordsToPixel({v.base.x, v.label_y}, camera.view());
        const sf::FloatRect b = text.getLocalBounds();
        text.setOrigin({b.position.x + b.size.x * 0.5f, b.position.y + b.size.y});
        text.setPosition({static_cast<float>(px.x), static_cast<float>(px.y)});
        window.draw(text);
    }
}

std::optional<core::EntityHandle> EntityRenderer::pick(sf::Vector2f iso) const {
    // De delante hacia atrás: la primera que contiene el punto es la que se ve encima
    for (auto it = visible_.rbegin(); it != visible_.rend(); ++it) {
        if (it->hit.contains(iso)) return it->handle;
    }
    return std::nullopt;
}

}  // namespace render
