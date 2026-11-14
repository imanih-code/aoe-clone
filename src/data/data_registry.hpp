#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

    const StartConfig& start() const { return start_; }

private:
    std::vector<ObjSheet> sheets_;
    std::map<std::string, TypeId, std::less<>> type_by_key_;
    std::vector<std::string> class_names_;
    std::map<std::string, ClassId, std::less<>> class_by_name_;
    StartConfig start_;
};

}  // namespace data
