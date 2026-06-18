#pragma once

#include <cubey/procedural/field_2d.h>

#include <string>
#include <string_view>
#include <vector>

namespace cubey::procedural {

struct FieldSet2DEntry {
    std::string name{};
    ScalarField2D field{};
};

class FieldSet2D {
  public:
    FieldSet2D() = default;
    explicit FieldSet2D(Grid2DDesc desc);

    [[nodiscard]] const Grid2DDesc& desc() const;
    [[nodiscard]] std::size_t field_count() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] bool has_field(std::string_view name) const;
    [[nodiscard]] const ScalarField2D* try_field(std::string_view name) const;
    [[nodiscard]] ScalarField2D* try_field(std::string_view name);
    [[nodiscard]] const ScalarField2D& field(std::string_view name) const;
    [[nodiscard]] ScalarField2D& field(std::string_view name);
    [[nodiscard]] std::vector<std::string> field_names() const;
    [[nodiscard]] ScalarFieldStats summarize_field(std::string_view name) const;

    void add_field(std::string name, ScalarField2D field);

  private:
    Grid2DDesc desc_{};
    std::vector<FieldSet2DEntry> fields_{};
};

} // namespace cubey::procedural
