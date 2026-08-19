#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace cubey::config {

// This is deliberately independent from any project option identifiers.
// Projects describe their own fields by composing schemas.
enum class ValueType {
    Bool,
    Int,
    UInt32,
    UInt64,
    Float,
    String,
    Path,
    Enum,
};

struct Range {
    bool has_min = false;
    bool has_max = false;
    double min = 0.0;
    double max = 0.0;
};

struct OptionSpec {
    std::string path;
    std::string cli_name;
    std::string negative_cli_name;
    std::string label;
    std::string group_path;
    std::string help;
    ValueType type = ValueType::String;
    Range range{};
    std::vector<std::string> enum_values;
};

using Value =
    std::variant<bool, std::int64_t, std::uint64_t, double, std::string, std::filesystem::path>;

struct ParseResult {
    std::optional<std::filesystem::path> write_config_template_path;
    std::unordered_set<std::string> assigned_paths;

    [[nodiscard]] bool path_was_assigned(std::string_view path) const {
        return assigned_paths.find(std::string(path)) != assigned_paths.end();
    }
};

class Schema {
  public:
    class Builder;

    Schema() = default;

    [[nodiscard]] static Builder builder();

    [[nodiscard]] std::span<const OptionSpec> options() const;
    [[nodiscard]] const OptionSpec* find(std::string_view path) const;
    [[nodiscard]] const OptionSpec* find_by_cli_name(std::string_view cli_name) const;

    void set(std::string_view path, std::string_view value) const;
    void set(std::string_view path, const nlohmann::json& value) const;
    void set(std::string_view path, const char* value) const {
        set(path, std::string_view(value));
    }
    void apply_json(const nlohmann::json& document) const;
    // Returns the non-null leaf paths supplied by the file so callers can
    // distinguish an explicit value from a default during normalization.
    [[nodiscard]] std::unordered_set<std::string>
    apply_file(const std::filesystem::path& path) const;

    // Parses bootstrap flags, config files, named flags, then deferred --set
    // assignments. This ordering is part of the generic config contract.
    [[nodiscard]] ParseResult parse_cli(int argc, char** argv) const;

    [[nodiscard]] nlohmann::json template_json() const;
    void write_template(const std::filesystem::path& path) const;

    // A binding's type-erased operations hold references/pointers to
    // caller-owned storage. That storage must outlive the schema and remain
    // at a stable address while the schema is used; moving or destroying it
    // invalidates parsing and template emission through the binding.
    struct Binding {
        OptionSpec spec;
        std::function<void(std::string_view)> set_text;
        std::function<void(const nlohmann::json&)> set_json;
        std::function<nlohmann::json()> get_json;
        std::optional<ValueType> storage_type;
    };

  private:
    explicit Schema(std::vector<Binding> bindings);
    static void validate_specs(std::span<const Binding> bindings);

    std::vector<Binding> bindings_;
    std::vector<OptionSpec> specs_;

    friend class Builder;
};

namespace detail {

template <typename T> struct optional_traits {
    static constexpr bool is_optional = false;
    using value_type = T;
};

template <typename T> struct optional_traits<std::optional<T>> {
    static constexpr bool is_optional = true;
    using value_type = T;
};

template <typename T> using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T>
inline constexpr bool is_string_v = std::is_same_v<remove_cvref_t<T>, std::string>;

template <typename T>
inline constexpr bool is_path_v = std::is_same_v<remove_cvref_t<T>, std::filesystem::path>;

template <typename T> inline constexpr bool is_bool_v = std::is_same_v<remove_cvref_t<T>, bool>;

template <typename T>
inline constexpr bool is_integral_v = std::is_integral_v<remove_cvref_t<T>> && !is_bool_v<T>;

template <typename T>
inline constexpr bool is_floating_v = std::is_floating_point_v<remove_cvref_t<T>>;

template <typename T> using value_type_t = typename optional_traits<remove_cvref_t<T>>::value_type;

template <typename T> inline constexpr bool is_enum_v = std::is_enum_v<value_type_t<T>>;

// Typed enum bindings map enum_values by ordinal: the first choice maps to
// static_cast<Enum>(0), the second to 1, and so on. Enum declarations used
// with bind() must therefore be zero-based and contiguous.
template <typename T> constexpr ValueType storage_value_type() {
    using U = value_type_t<T>;
    if constexpr (is_bool_v<U>) {
        return ValueType::Bool;
    } else if constexpr (is_string_v<U>) {
        return ValueType::String;
    } else if constexpr (is_path_v<U>) {
        return ValueType::Path;
    } else if constexpr (is_enum_v<T>) {
        return ValueType::Enum;
    } else if constexpr (std::is_integral_v<U>) {
        if constexpr (std::is_signed_v<U>) {
            return ValueType::Int;
        } else if constexpr (sizeof(U) <= sizeof(std::uint32_t)) {
            return ValueType::UInt32;
        } else {
            return ValueType::UInt64;
        }
    } else if constexpr (std::is_floating_point_v<U>) {
        return ValueType::Float;
    } else {
        static_assert(!sizeof(T), "unsupported config binding type");
    }
}

template <typename T>
value_type_t<T> value_from_variant(const Value& value, const OptionSpec& spec) {
    using U = value_type_t<T>;
    if constexpr (is_bool_v<U>) {
        return std::get<bool>(value);
    } else if constexpr (is_string_v<U>) {
        return std::get<std::string>(value);
    } else if constexpr (is_path_v<U>) {
        return std::get<std::filesystem::path>(value);
    } else if constexpr (is_enum_v<T>) {
        const std::string& text = std::get<std::string>(value);
        const auto it = std::find(spec.enum_values.begin(), spec.enum_values.end(), text);
        if (it == spec.enum_values.end()) {
            throw std::runtime_error("invalid enum value for " + spec.path);
        }
        return static_cast<U>(std::distance(spec.enum_values.begin(), it));
    } else if constexpr (std::is_integral_v<U>) {
        if constexpr (std::is_signed_v<U>) {
            const std::int64_t number =
                std::holds_alternative<std::int64_t>(value)
                    ? std::get<std::int64_t>(value)
                    : static_cast<std::int64_t>(std::get<std::uint64_t>(value));
            if (number < static_cast<std::int64_t>(std::numeric_limits<U>::min()) ||
                number > static_cast<std::int64_t>(std::numeric_limits<U>::max())) {
                throw std::runtime_error("integer is outside the storage type for " + spec.path);
            }
            return static_cast<U>(number);
        } else {
            const std::uint64_t number =
                std::holds_alternative<std::uint64_t>(value)
                    ? std::get<std::uint64_t>(value)
                    : static_cast<std::uint64_t>(std::get<std::int64_t>(value));
            if (number > static_cast<std::uint64_t>(std::numeric_limits<U>::max())) {
                throw std::runtime_error("unsigned integer is outside the storage type for " +
                                         spec.path);
            }
            return static_cast<U>(number);
        }
    } else if constexpr (std::is_floating_point_v<U>) {
        return static_cast<U>(std::holds_alternative<double>(value)
                                  ? std::get<double>(value)
                                  : (std::holds_alternative<std::int64_t>(value)
                                         ? static_cast<double>(std::get<std::int64_t>(value))
                                         : static_cast<double>(std::get<std::uint64_t>(value))));
    } else {
        static_assert(!sizeof(T), "unsupported config binding type");
    }
}

template <typename T> void assign(T& storage, const Value& value, const OptionSpec& spec) {
    using U = remove_cvref_t<T>;
    if constexpr (optional_traits<U>::is_optional) {
        storage = value_from_variant<U>(value, spec);
    } else {
        storage = value_from_variant<U>(value, spec);
    }
}

template <typename T> nlohmann::json to_json(const T& storage, const OptionSpec& spec) {
    using U = remove_cvref_t<T>;
    if constexpr (optional_traits<U>::is_optional) {
        if (!storage.has_value()) {
            return nullptr;
        }
        return to_json(*storage, spec);
    } else if constexpr (is_bool_v<U>) {
        return storage;
    } else if constexpr (is_string_v<U>) {
        return storage;
    } else if constexpr (is_path_v<U>) {
        return storage.string();
    } else if constexpr (is_enum_v<U>) {
        const auto index = static_cast<std::size_t>(storage);
        if (index >= spec.enum_values.size()) {
            return nullptr;
        }
        return spec.enum_values[index];
    } else if constexpr (std::is_integral_v<U> || std::is_floating_point_v<U>) {
        return storage;
    } else {
        static_assert(!sizeof(T), "unsupported config binding type");
    }
}

} // namespace detail

class Schema::Builder {
  public:
    Builder() = default;

    template <typename T> Builder& bind(OptionSpec spec, T& storage) {
        static_assert(!std::is_const_v<T>, "config bindings require mutable storage");
        Binding binding;
        binding.spec = std::move(spec);
        binding.storage_type = detail::storage_value_type<T>();
        T* target = &storage;
        binding.set_text = [target, spec_copy = binding.spec](std::string_view value) {
            const Value parsed = parse_text(value, spec_copy);
            detail::assign(*target, parsed, spec_copy);
        };
        binding.set_json = [target, spec_copy = binding.spec](const nlohmann::json& value) {
            if (value.is_null()) {
                return;
            }
            const Value parsed = parse_json(value, spec_copy);
            detail::assign(*target, parsed, spec_copy);
        };
        binding.get_json = [target, spec_copy = binding.spec] {
            return detail::to_json(*target, spec_copy);
        };
        bindings_.push_back(std::move(binding));
        return *this;
    }

    Builder& compose(const Schema& schema);
    Builder& bind_custom(OptionSpec spec, std::function<void(std::string_view)> set_text,
                         std::function<void(const nlohmann::json&)> set_json,
                         std::function<nlohmann::json()> get_json);
    [[nodiscard]] Schema build() const&;
    [[nodiscard]] Schema build() &&;

  private:
    static Value parse_text(std::string_view value, const OptionSpec& spec);
    static Value parse_json(const nlohmann::json& value, const OptionSpec& spec);

    std::vector<Schema::Binding> bindings_;
};

} // namespace cubey::config
