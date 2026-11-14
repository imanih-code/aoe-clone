#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "core/entity_handle.hpp"

namespace core {

// Almacén contiguo con handles generacionales.
// - get() devuelve nullptr si el handle está muerto.
// - Los punteros devueltos solo son válidos hasta el siguiente insert().
// - for_each recorre en orden de índice: iteración determinista.
template <class T>
class SlotMap {
public:
    EntityHandle insert(T value) {
        uint32_t index;
        if (!free_list_.empty()) {
            index = free_list_.back();
            free_list_.pop_back();
        } else {
            index = static_cast<uint32_t>(slots_.size());
            slots_.emplace_back();
        }
        slots_[index].value.emplace(std::move(value));
        ++count_;
        return EntityHandle{index, slots_[index].generation};
    }

    bool erase(EntityHandle h) {
        Slot* s = slot(h);
        if (!s) return false;
        s->value.reset();
        ++s->generation;
        free_list_.push_back(h.index);
        --count_;
        return true;
    }

    T* get(EntityHandle h) {
        Slot* s = slot(h);
        return s ? &*s->value : nullptr;
    }

    const T* get(EntityHandle h) const {
        const Slot* s = slot(h);
        return s ? &*s->value : nullptr;
    }

    bool contains(EntityHandle h) const { return slot(h) != nullptr; }
    std::size_t size() const { return count_; }

    template <class F>
    void for_each(F&& f) {
        for (uint32_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i].value) f(EntityHandle{i, slots_[i].generation}, *slots_[i].value);
        }
    }

    template <class F>
    void for_each(F&& f) const {
        for (uint32_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i].value) f(EntityHandle{i, slots_[i].generation}, *slots_[i].value);
        }
    }

private:
    struct Slot {
        std::optional<T> value;
        uint32_t generation = 0;
    };

    Slot* slot(EntityHandle h) {
        if (h.index >= slots_.size()) return nullptr;
        Slot& s = slots_[h.index];
        return (s.value && s.generation == h.generation) ? &s : nullptr;
    }

    const Slot* slot(EntityHandle h) const {
        if (h.index >= slots_.size()) return nullptr;
        const Slot& s = slots_[h.index];
        return (s.value && s.generation == h.generation) ? &s : nullptr;
    }

    std::vector<Slot> slots_;
    std::vector<uint32_t> free_list_;
    std::size_t count_ = 0;
};

}  // namespace core
