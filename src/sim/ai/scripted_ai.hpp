#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "core/entity_handle.hpp"
#include "core/fixed.hpp"
#include "data/ids.hpp"
#include "sim/commands/command.hpp"

namespace sim {

class GameState;
struct SpawnableEntity;

// IA por guion (architecture.md §11.5). Reglas:
// - Solo LEE el estado y EMITE Commands, igual que un jugador humano.
// - Respeta la niebla: solo ataca lo que ve y solo construye en terreno explorado.
//   Como un jugador que ha visto el mapa al empezar, conoce las posiciones iniciales.
// - Es determinista: misma partida → mismas órdenes (los replays la incluyen igualmente).
class ScriptedAI {
public:
    explicit ScriptedAI(data::PlayerId player) : me_(player) {}

    std::vector<Command> think(const GameState& game);
    data::PlayerId player() const { return me_; }

private:
    struct View;  // resumen del estado propio que se recalcula en cada think()

    void economy(const GameState& game, const View& v, std::vector<Command>& out);
    void buildings(const GameState& game, const View& v, std::vector<Command>& out);
    void military(const GameState& game, const View& v, std::vector<Command>& out);
    void assign_idle_villagers(const GameState& game, const View& v, std::vector<Command>& out);

    std::optional<core::TilePos> find_spot(const GameState& game, data::TypeId building, core::TilePos near, int min_r,
                                           int max_r) const;
    bool place(const GameState& game, const View& v, const char* key, core::TilePos near, int min_r, int max_r,
               std::vector<Command>& out);
    Command make(CommandType type, std::vector<core::EntityHandle> actors) const;

    data::PlayerId me_;
    int wave_ = 0;              // oleadas de ataque enviadas
    uint32_t next_wave_tick_ = 0;
    bool scouted_ = false;
    std::vector<core::EntityHandle> attacking_;  // unidades ya enviadas en una oleada
    std::vector<core::EntityHandle> reserved_;   // aldeanos con orden en este think() (no reasignarlos)
    bool farm_placed_ = false;
    std::vector<core::EntityHandle> hunted_;     // ganado propio que ya tiene a alguien asignado
};

}  // namespace sim
