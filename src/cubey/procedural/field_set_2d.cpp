#include <cubey/procedural/field_set_2d.h>

#include <stdexcept>
#include <utility>

namespace cubey::procedural {
namespace {

void require_valid_field_name(std::string_view name) {
    if (name.empty()) {
        throw std::runtime_error("procedural field set names must be non-empty");
    }
}

} // namespace

FieldSet2D::FieldSet2D(Grid2DDesc desc) : desc_(desc) {
    if (desc_.width == 0U || desc_.height == 0U) {
        throw std::runtime_error("procedural field set dimensions must be non-zero");
    }
}

const Grid2DDesc& FieldSet2D::desc() const {
    return desc_;
}

std::size_t FieldSet2D::field_count() const {
    return fields_.size();
}

bool FieldSet2D::empty() const {
    return fields_.empty();
}

bool FieldSet2D::has_field(std::string_view name) const {
    return try_field(name) != nullptr;
}

const ScalarField2D* FieldSet2D::try_field(std::string_view name) const {
    for (const FieldSet2DEntry& entry : fields_) {
        if (entry.name == name) {
            return &entry.field;
        }
    }
    return nullptr;
}

ScalarField2D* FieldSet2D::try_field(std::string_view name) {
    for (FieldSet2DEntry& entry : fields_) {
        if (entry.name == name) {
            return &entry.field;
        }
    }
    return nullptr;
}

const ScalarField2D& FieldSet2D::field(std::string_view name) const {
    if (const ScalarField2D* result = try_field(name)) {
        return *result;
    }
    throw std::runtime_error("procedural field set field was not found");
}

ScalarField2D& FieldSet2D::field(std::string_view name) {
    if (ScalarField2D* result = try_field(name)) {
        return *result;
    }
    throw std::runtime_error("procedural field set field was not found");
}

std::vector<std::string> FieldSet2D::field_names() const {
    std::vector<std::string> result;
    result.reserve(fields_.size());
    for (const FieldSet2DEntry& entry : fields_) {
        result.push_back(entry.name);
    }
    return result;
}

ScalarFieldStats FieldSet2D::summarize_field(std::string_view name) const {
    return field(name).summarize();
}

void FieldSet2D::add_field(std::string name, ScalarField2D field) {
    require_valid_field_name(name);
    if (has_field(name)) {
        throw std::runtime_error("procedural field set names must be unique");
    }
    if (!same_grid_desc(desc_, field.desc())) {
        throw std::runtime_error("procedural field set descriptors must match");
    }
    fields_.push_back(FieldSet2DEntry{.name = std::move(name), .field = std::move(field)});
}

} // namespace cubey::procedural
