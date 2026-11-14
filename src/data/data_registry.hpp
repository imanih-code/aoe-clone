#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "data/ids.hpp"
#include "data/obj_sheet.hpp"

namespace data {

class DataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Contenido del juego cargado de data/. No es singleton: se pasa por referencia
// para que los tests puedan cargar sus propios datos.
class DataRegistry {
public:
    // Lanza DataError con el nombre del fichero si algo no cuadra.
    static DataRegistry load(const std::filesystem::path& data_dir);

    const ObjSheet& sheet(TypeId t) const { return sheets_.at(t.value); }
    const std::vector<ObjSheet>& sheets() const { return sheets_; }
    std::optional<TypeId> find_type(std::string_view key) const;
    TypeId type(std::string_view key) const;  // lanza DataError si no existe

    std::optional<ClassId> find_class(std::string_view name) const;
    const std::vector<std::string>& class_names() const { return class_names_; }
    std::optional<ArmorClassId> find_armor_class(std::string_view name) const;
    const std::vector<std::string>& armor_class_names() const { return armor_class_names_; }

    const std::vector<std::string>& age_names() const { return age_names_; }

    const std::vector<TechTemplate>& techs() const { return techs_; }
    const TechTemplate& tech(TechId id) const { return techs_.at(id); }
    std::optional<TechId> find_tech(std::string_view key) const;

    const std::vector<CivTemplate>& civs() const { return civs_; }
    std::optional<std::size_t> find_civ(std::string_view key) const;

    const StartConfig& start() const { return start_; }

private:
    Modifier parse_modifier(const nlohmann::json& j, const std::string& where) const;

    std::vector<ObjSheet> sheets_;
    std::map<std::string, TypeId, std::less<>> type_by_key_;
    std::vector<std::string> class_names_;
    std::map<std::string, ClassId, std::less<>> class_by_name_;
    std::vector<std::string> armor_class_names_;
    std::vector<std::string> age_names_;
    std::vector<TechTemplate> techs_;
    std::map<std::string, TechId, std::less<>> tech_by_key_;
    std::vector<CivTemplate> civs_;
    StartConfig start_;
};

}  // namespace data
