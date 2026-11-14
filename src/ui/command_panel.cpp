#include "ui/command_panel.hpp"

#include <algorithm>
#include <array>

#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>

#include "render/text.hpp"

namespace ui {

namespace {

using K = sf::Keyboard::Key;

// Cuadrícula de atajos: la posición del botón decide su tecla
constexpr std::array<K, 15> kGridKeys = {K::Q, K::W, K::E, K::R, K::T, K::A, K::S, K::D,
                                         K::F, K::G, K::Z, K::X, K::C, K::V, K::B};
constexpr std::array<char, 15> kGridChars = {'Q', 'W', 'E', 'R', 'T', 'A', 'S', 'D', 'F', 'G', 'Z', 'X', 'C', 'V', 'B'};
constexpr int kCols = 5;
constexpr int kRows = 3;
constexpr float kBtnW = 74.f;
constexpr float kBtnH = 42.f;
constexpr float kGap = 6.f;
constexpr float kPad = 10.f;
constexpr std::size_t kStopSlot = 14;  // B

const char* resource_word(std::size_t r) {
    static const char* names[] = {"comida", "madera", "oro", "piedra"};
    return names[r];
}

std::string seconds_text(uint32_t ticks) {
    return std::to_string((ticks + sim::kTicksPerSecond - 1) / sim::kTicksPerSecond) + " s";
}

}  // namespace

std::string cost_text(const data::ResourceArray& cost) {
    std::string out;
    for (std::size_t i = 0; i < data::kResourceCount; ++i) {
        if (cost[i] == 0) continue;
        if (!out.empty()) out += " · ";
        out += std::to_string(cost[i]) + " " + resource_word(i);
    }
    return out.empty() ? "gratis" : out;
}

void CommandPanel::rebuild(const sim::GameState& game, data::PlayerId local, std::span<const core::EntityHandle> selection,
                           sf::Vector2u window_size) {
    buttons_.clear();
    has_queue_ = false;

    const auto& data = game.data();
    const sim::PlayerState& me = game.player(local);
    auto age_name = [&](uint8_t age) {
        return age < data.age_names().size() ? data.age_names()[age] : std::string("edad ") + std::to_string(age);
    };

    std::vector<data::TypeId> builds;
    bool any_unit = false;
    const sim::SpawnableEntity* producer = nullptr;  // edificio que entrena o investiga
    for (core::EntityHandle h : selection) {
        const sim::SpawnableEntity* e = game.entity(h);
        if (!e || e->current_player != local) continue;
        const auto& s = game.sheet_of(*e);
        if (s.is_unit()) {
            any_unit = true;
            for (auto t : game.buildable_by(local, s)) {
                if (std::find(builds.begin(), builds.end(), t) == builds.end()) builds.push_back(t);
            }
        } else if (!producer && s.is_building() && e->is_complete() &&
                   (!game.trainable_at(local, e->type).empty() || !game.techs_at(local, e->type).empty())) {
            producer = e;
        }
    }

    auto add = [&](std::size_t slot, Button b) {
        if (slot >= kGridKeys.size()) return;
        b.hotkey = kGridKeys[slot];
        b.hotkey_char = kGridChars[slot];
        b.index = static_cast<int>(slot);
        buttons_.push_back(std::move(b));
    };

    if (any_unit) {
        std::size_t slot = 0;
        for (data::TypeId t : builds) {
            const auto& s = data.sheet(t);
            Button b;
            b.kind = ButtonKind::Build;
            b.type = t;
            b.label = s.visual.label;
            const bool age_ok = me.age >= s.stats.min_age;
            b.enabled = age_ok && game.can_afford(local, s.stats.cost);
            b.tooltip = "Construir " + s.display_name + "\n" + cost_text(s.stats.cost) + "  ·  " + seconds_text(s.stats.build_ticks);
            if (s.stats.population_provided > 0) b.tooltip += "\n+" + std::to_string(s.stats.population_provided) + " de población";
            if (!s.description.empty()) b.tooltip += "\n" + s.description;
            if (!age_ok) b.tooltip += "\nRequiere " + age_name(s.stats.min_age);
            else if (!b.enabled) b.tooltip += "\nFaltan recursos";
            add(slot++, std::move(b));
            if (slot == kStopSlot) break;
        }
        Button stop;
        stop.kind = ButtonKind::Stop;
        stop.label = "Detener";
        stop.tooltip = "Detener\nCancela la orden actual";
        add(kStopSlot, std::move(stop));
    } else if (producer) {
        std::size_t slot = 0;
        const bool queue_full = producer->production.size() >= sim::kMaxQueue;
        // Unidades: ya resueltas con la civilización y las líneas de mejora
        for (data::TypeId t : game.trainable_at(local, producer->type)) {
            const auto& s = data.sheet(t);
            Button b;
            b.kind = ButtonKind::Train;
            b.type = t;
            b.label = s.visual.label;
            const bool age_ok = me.age >= s.stats.min_age;
            b.enabled = age_ok && game.can_afford(local, s.stats.cost) && !queue_full;
            b.tooltip = "Entrenar " + s.display_name + "\n" + cost_text(s.stats.cost) + "  ·  " + seconds_text(s.stats.train_ticks);
            if (!s.description.empty()) b.tooltip += "\n" + s.description;
            if (!age_ok) b.tooltip += "\nRequiere " + age_name(s.stats.min_age);
            else if (!game.can_afford(local, s.stats.cost)) b.tooltip += "\nFaltan recursos";
            add(slot++, std::move(b));
        }
        // Tecnologías (incluido subir de edad)
        for (data::TechId id : game.techs_at(local, producer->type)) {
            const auto& tech = data.tech(id);
            const sim::TechStatus status = game.tech_status(local, id);
            Button b;
            b.kind = ButtonKind::Research;
            b.tech = id;
            b.label = tech.name;
            b.is_tech = true;
            b.enabled = status == sim::TechStatus::Available && game.can_afford(local, tech.cost) && !queue_full;
            b.tooltip = "Investigar " + tech.name + "\n" + cost_text(tech.cost) + "  ·  " + seconds_text(tech.research_ticks);
            if (!tech.description.empty()) b.tooltip += "\n" + tech.description;
            switch (status) {
            case sim::TechStatus::Queued: b.tooltip += "\nYa se está investigando"; break;
            case sim::TechStatus::NeedsAge: b.tooltip += "\nRequiere " + age_name(tech.min_age); break;
            case sim::TechStatus::NeedsTech:
                for (data::TechId req : tech.requires_techs) {
                    if (!me.has_researched(req)) b.tooltip += "\nRequiere " + data.tech(req).name;
                }
                break;
            default:
                if (!game.can_afford(local, tech.cost)) b.tooltip += "\nFaltan recursos";
            }
            add(slot++, std::move(b));
        }
        // Cola: clic para cancelar (se devuelve el coste)
        for (std::size_t i = 0; i < producer->production.size(); ++i) {
            const auto& job = producer->production[i];
            Button q;
            q.kind = ButtonKind::CancelQueue;
            q.index = static_cast<int>(i);
            q.is_tech = job.tech.has_value();
            if (job.tech) {
                q.label = data.tech(*job.tech).name;
                q.tooltip = "Cancelar " + data.tech(*job.tech).name + "\nSe devuelve " + cost_text(job.paid);
            } else {
                q.type = job.type;
                q.label = data.sheet(job.type).visual.label;
                q.tooltip = "Cancelar " + data.sheet(job.type).display_name + "\nSe devuelve " + cost_text(job.paid);
            }
            if (i == 0) {
                q.progress = 1.f - static_cast<float>(job.ticks_remaining) / static_cast<float>(std::max<uint32_t>(1, job.ticks_total));
            }
            buttons_.push_back(std::move(q));
            has_queue_ = true;
        }
    }

    visible_ = !buttons_.empty();
    if (!visible_) return;

    // ─── Geometría ───────────────────────────────────────────────────────
    const float w = kPad * 2.f + kCols * kBtnW + (kCols - 1) * kGap;
    const float h = kPad * 2.f + kRows * kBtnH + (kRows - 1) * kGap;
    const float x = (static_cast<float>(window_size.x) - w) * 0.5f;
    const float y = static_cast<float>(window_size.y) - h - 8.f;
    grid_area_ = sf::FloatRect({x, y}, {w, h});
    const float qsize = 38.f;
    queue_area_ = sf::FloatRect({x, y - qsize - 2.f * kGap}, {w, qsize + kGap});

    int queued = 0;
    for (Button& b : buttons_) {
        if (b.kind == ButtonKind::CancelQueue) {
            b.rect = sf::FloatRect({x + kPad + static_cast<float>(queued) * (qsize + 4.f), y - qsize - kGap}, {qsize, qsize});
            b.hotkey = K::Unknown;
            b.hotkey_char = 0;
            ++queued;
        } else {
            const int slot = std::max(0, b.index);
            const float bx = x + kPad + static_cast<float>(slot % kCols) * (kBtnW + kGap);
            const float by = y + kPad + static_cast<float>(slot / kCols) * (kBtnH + kGap);
            b.rect = sf::FloatRect({bx, by}, {kBtnW, kBtnH});
        }
    }
}

const Button* CommandPanel::hit(sf::Vector2f pixel) const {
    for (const Button& b : buttons_) {
        if (b.rect.contains(pixel)) return &b;
    }
    return nullptr;
}

const Button* CommandPanel::by_key(sf::Keyboard::Key key) const {
    if (key == K::Unknown) return nullptr;
    for (const Button& b : buttons_) {
        if (b.hotkey == key) return &b;
    }
    return nullptr;
}

bool CommandPanel::contains(sf::Vector2f pixel) const {
    return visible_ && (grid_area_.contains(pixel) || (has_queue_ && queue_area_.contains(pixel)));
}

void CommandPanel::draw(sf::RenderWindow& window, const sf::Font& font, sf::Vector2f mouse) const {
    if (!visible_) return;

    sf::RectangleShape bg(grid_area_.size);
    bg.setPosition(grid_area_.position);
    bg.setFillColor(sf::Color(28, 22, 16, 225));
    bg.setOutlineColor(sf::Color(120, 100, 60));
    bg.setOutlineThickness(1.f);
    window.draw(bg);

    sf::Text text(font, "", 12);
    const Button* hovered = hit(mouse);

    for (const Button& b : buttons_) {
        const bool is_hover = &b == hovered;
        sf::RectangleShape r(b.rect.size);
        r.setPosition(b.rect.position);
        sf::Color fill = b.kind == ButtonKind::CancelQueue ? sf::Color(52, 60, 78) : sf::Color(70, 58, 40);
        if (b.is_tech) fill = b.kind == ButtonKind::CancelQueue ? sf::Color(60, 50, 82) : sf::Color(48, 62, 74);  // tecnologías en azul
        if (!b.enabled) fill = sf::Color(48, 44, 40);
        if (is_hover && b.enabled) fill = render::mix(fill, sf::Color::White, 0.18f);
        r.setFillColor(fill);
        r.setOutlineColor(is_hover ? sf::Color(240, 210, 140) : sf::Color(140, 116, 70));
        r.setOutlineThickness(1.f);
        window.draw(r);

        if (b.progress >= 0.f) {
            sf::RectangleShape bar({b.rect.size.x * std::clamp(b.progress, 0.f, 1.f), 4.f});
            bar.setPosition({b.rect.position.x, b.rect.position.y + b.rect.size.y - 4.f});
            bar.setFillColor(sf::Color(90, 210, 90));
            window.draw(bar);
        }

        // Etiqueta centrada (se recorta a dos líneas si no cabe)
        std::string label = b.label;
        unsigned size = b.kind == ButtonKind::CancelQueue ? 9 : 12;
        text.setCharacterSize(size);
        text.setString(render::utf8(label));
        if (text.getLocalBounds().size.x > b.rect.size.x - 6.f) {
            if (auto sp = label.find(' '); sp != std::string::npos) label[sp] = '\n';
            text.setCharacterSize(--size);
            text.setString(render::utf8(label));
        }
        text.setFillColor(b.enabled ? sf::Color(240, 232, 210) : sf::Color(130, 124, 116));
        const sf::FloatRect lb = text.getLocalBounds();
        text.setOrigin({lb.position.x + lb.size.x * 0.5f, lb.position.y + lb.size.y * 0.5f});
        // Un poco más abajo del centro para no tapar la tecla de la esquina
        text.setPosition(b.rect.position + b.rect.size * 0.5f + sf::Vector2f{0.f, b.hotkey_char ? 4.f : 0.f});
        window.draw(text);

        if (b.hotkey_char) {
            text.setCharacterSize(10);
            text.setString(std::string(1, b.hotkey_char));
            text.setOrigin({0.f, 0.f});
            text.setPosition(b.rect.position + sf::Vector2f{3.f, 1.f});
            text.setFillColor(sf::Color(250, 210, 120));
            window.draw(text);
        }
    }

    // ─── Tooltip ─────────────────────────────────────────────────────────
    if (hovered && !hovered->tooltip.empty()) {
        std::string tip = hovered->tooltip;
        if (hovered->hotkey_char) {
            const auto nl = tip.find('\n');
            tip.insert(nl == std::string::npos ? tip.size() : nl, std::string("  (") + hovered->hotkey_char + ")");
        }
        text.setCharacterSize(13);
        text.setString(render::utf8(tip));
        text.setOrigin({0.f, 0.f});
        text.setFillColor(sf::Color(240, 236, 220));
        const sf::FloatRect tb = text.getLocalBounds();
        const float top = (has_queue_ ? queue_area_.position.y : grid_area_.position.y) - tb.size.y - 22.f;
        sf::RectangleShape box({tb.size.x + 16.f, tb.size.y + 14.f});
        box.setPosition({grid_area_.position.x, top});
        box.setFillColor(sf::Color(16, 14, 10, 235));
        box.setOutlineColor(sf::Color(160, 130, 80));
        box.setOutlineThickness(1.f);
        window.draw(box);
        text.setPosition({grid_area_.position.x + 8.f - tb.position.x, top + 6.f - tb.position.y});
        window.draw(text);
    }
}

}  // namespace ui
