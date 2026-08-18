#include <cubey/core/config_schema.h>

#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace cubey::config {
namespace {

[[nodiscard]] std::string path_string(std::string_view path) {
    return std::string(path);
}

[[nodiscard]] bool valid_path(std::string_view path) {
    if (path.empty() || path.front() == '.' || path.back() == '.') {
        return false;
    }
    for (std::size_t begin = 0; begin < path.size();) {
        const std::size_t dot = path.find('.', begin);
        if (dot == begin) {
            return false;
        }
        if (dot == std::string_view::npos) {
            break;
        }
        begin = dot + 1U;
    }
    return true;
}

[[nodiscard]] bool is_bootstrap_alias(std::string_view alias) {
    return alias == "--config" || alias == "--set" || alias == "--write-config-template";
}

template <typename T>
[[nodiscard]] T parse_integer(std::string_view value, const OptionSpec& spec,
                              std::string_view kind) {
    T result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::runtime_error("invalid " + std::string(kind) + " for " + spec.path);
    }
    return result;
}

[[nodiscard]] double parse_float(std::string_view value, const OptionSpec& spec) {
    double result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        !std::isfinite(result)) {
        throw std::runtime_error("invalid float for " + spec.path);
    }
    return result;
}

[[nodiscard]] bool parse_bool(std::string_view value, const OptionSpec& spec) {
    if (value == "true" || value == "1" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "false" || value == "0" || value == "no" || value == "off") {
        return false;
    }
    throw std::runtime_error("invalid bool for " + spec.path);
}

void validate_number(double value, const OptionSpec& spec) {
    if (spec.range.has_min && value < spec.range.min) {
        throw std::runtime_error(spec.path + " is below its minimum");
    }
    if (spec.range.has_max && value > spec.range.max) {
        throw std::runtime_error(spec.path + " is above its maximum");
    }
}

[[nodiscard]] bool storage_type_matches(ValueType declared, ValueType storage) {
    // String storage remains useful for dynamically named enum values; typed
    // enum storage is the preferred binding for project-owned choices.
    return declared == storage || (declared == ValueType::Enum && storage == ValueType::String);
}

[[nodiscard]] std::string json_path(std::string_view prefix, std::string_view key) {
    return prefix.empty() ? std::string(key) : std::string(prefix) + "." + std::string(key);
}

[[nodiscard]] bool json_type_matches(const nlohmann::json& value, const OptionSpec& spec) {
    if (value.is_null()) {
        return true;
    }
    switch (spec.type) {
    case ValueType::Bool:
        return value.is_boolean();
    case ValueType::Int:
        return value.is_number_integer();
    case ValueType::UInt32:
    case ValueType::UInt64:
        return value.is_number_unsigned() ||
               (value.is_number_integer() && value.get<std::int64_t>() >= 0);
    case ValueType::Float:
        return value.is_number();
    case ValueType::String:
    case ValueType::Path:
    case ValueType::Enum:
        return value.is_string();
    }
    return false;
}

void validate_json_tree(const Schema& schema, const nlohmann::json& object,
                        std::string_view prefix) {
    if (!object.is_object()) {
        throw std::runtime_error("config file root must be a JSON object");
    }
    for (const auto& [key, value] : object.items()) {
        const std::string path = json_path(prefix, key);
        if (value.is_object()) {
            if (schema.find(path) != nullptr) {
                throw std::runtime_error("wrong JSON type for config option: " + path);
            }
            const std::string child_prefix = path + ".";
            const bool known_group = std::any_of(schema.options().begin(), schema.options().end(),
                                                 [&child_prefix](const OptionSpec& spec) {
                                                     return spec.path.rfind(child_prefix, 0U) == 0U;
                                                 });
            if (!known_group) {
                throw std::runtime_error("unknown config option: " + path);
            }
            validate_json_tree(schema, value, path);
            continue;
        }
        const OptionSpec* spec = schema.find(path);
        if (spec == nullptr) {
            throw std::runtime_error("unknown config option: " + path);
        }
        if (!json_type_matches(value, *spec)) {
            throw std::runtime_error("wrong JSON type for config option: " + path);
        }
        if (value.is_null()) {
            continue;
        }
        if (spec->type == ValueType::Enum) {
            const std::string selected = value.get<std::string>();
            if (std::find(spec->enum_values.begin(), spec->enum_values.end(), selected) ==
                spec->enum_values.end()) {
                throw std::runtime_error("invalid enum value for config option: " + path);
            }
        }
        if (spec->type == ValueType::Float) {
            const double number = value.get<double>();
            if (!std::isfinite(number)) {
                throw std::runtime_error("invalid float for config option: " + path);
            }
            validate_number(number, *spec);
        }
        if (spec->type == ValueType::Int || spec->type == ValueType::UInt32 ||
            spec->type == ValueType::UInt64) {
            validate_number(value.get<double>(), *spec);
        }
    }
}

void apply_json_tree(const Schema& schema, const nlohmann::json& object, std::string_view prefix) {
    for (const auto& [key, value] : object.items()) {
        const std::string path = json_path(prefix, key);
        if (value.is_object()) {
            apply_json_tree(schema, value, path);
        } else if (!value.is_null()) {
            schema.set(path, value);
        }
    }
}

void collect_json_paths(const nlohmann::json& object, std::string_view prefix,
                        std::unordered_set<std::string>& paths) {
    for (const auto& [key, value] : object.items()) {
        const std::string path = json_path(prefix, key);
        if (value.is_object()) {
            collect_json_paths(value, path, paths);
        } else if (!value.is_null()) {
            paths.insert(path);
        }
    }
}

void set_json_path(nlohmann::json& root, std::string_view path, nlohmann::json value) {
    nlohmann::json* cursor = &root;
    std::size_t begin = 0;
    while (begin < path.size()) {
        const std::size_t dot = path.find('.', begin);
        const std::string key(path.substr(
            begin, dot == std::string_view::npos ? std::string_view::npos : dot - begin));
        if (dot == std::string_view::npos) {
            (*cursor)[key] = std::move(value);
            return;
        }
        cursor = &(*cursor)[key];
        begin = dot + 1U;
    }
}

} // namespace

Schema::Schema(std::vector<Binding> bindings) : bindings_(std::move(bindings)) {
    validate_specs(bindings_);
    specs_.reserve(bindings_.size());
    for (const Binding& binding : bindings_) {
        specs_.push_back(binding.spec);
    }
}

Schema::Builder Schema::builder() {
    return {};
}

std::span<const OptionSpec> Schema::options() const {
    return specs_;
}

const OptionSpec* Schema::find(std::string_view path) const {
    const auto it =
        std::find_if(bindings_.begin(), bindings_.end(),
                     [path](const Binding& binding) { return binding.spec.path == path; });
    return it == bindings_.end() ? nullptr : &it->spec;
}

const OptionSpec* Schema::find_by_cli_name(std::string_view cli_name) const {
    const auto it =
        std::find_if(bindings_.begin(), bindings_.end(), [cli_name](const Binding& binding) {
            return binding.spec.cli_name == cli_name ||
                   (!binding.spec.negative_cli_name.empty() &&
                    binding.spec.negative_cli_name == cli_name);
        });
    return it == bindings_.end() ? nullptr : &it->spec;
}

void Schema::set(std::string_view path, std::string_view value) const {
    const auto it =
        std::find_if(bindings_.begin(), bindings_.end(),
                     [path](const Binding& binding) { return binding.spec.path == path; });
    if (it == bindings_.end()) {
        throw std::runtime_error("unknown config option: " + path_string(path));
    }
    it->set_text(value);
}

void Schema::set(std::string_view path, const nlohmann::json& value) const {
    const auto it =
        std::find_if(bindings_.begin(), bindings_.end(),
                     [path](const Binding& binding) { return binding.spec.path == path; });
    if (it == bindings_.end()) {
        throw std::runtime_error("unknown config option: " + path_string(path));
    }
    it->set_json(value);
}

void Schema::apply_json(const nlohmann::json& document) const {
    validate_json_tree(*this, document, {});
    apply_json_tree(*this, document, {});
}

std::unordered_set<std::string> Schema::apply_file(const std::filesystem::path& path) const {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("could not open config file: " + path.string());
    }
    nlohmann::json document;
    try {
        input >> document;
    } catch (const std::exception& error) {
        throw std::runtime_error("could not parse config file " + path.string() + ": " +
                                 error.what());
    }
    apply_json(document);
    std::unordered_set<std::string> assigned_paths;
    collect_json_paths(document, {}, assigned_paths);
    return assigned_paths;
}

ParseResult Schema::parse_cli(int argc, char** argv) const {
    std::vector<std::filesystem::path> config_files;
    struct NamedAssignment {
        std::string path;
        std::string value;
    };
    std::vector<NamedAssignment> named_assignments;
    std::vector<std::string> deferred_sets;
    ParseResult result;

    auto need_value = [&](int& index, std::string_view name) -> std::string_view {
        if (index + 1 >= argc) {
            throw std::runtime_error("missing value for " + std::string(name));
        }
        ++index;
        return argv[index];
    };

    // Collect each argument and its value exactly once. In particular, a
    // named option may legitimately receive a value that spells like one of
    // the bootstrap flags, so a second token-based scan would be ambiguous.
    for (int index = 1; index < argc; ++index) {
        const std::string_view arg(argv[index]);
        if (arg == "--config") {
            config_files.emplace_back(std::string(need_value(index, arg)));
        } else if (arg == "--set") {
            deferred_sets.emplace_back(need_value(index, arg));
        } else if (arg == "--write-config-template") {
            result.write_config_template_path =
                std::filesystem::path(std::string(need_value(index, arg)));
        } else {
            const OptionSpec* spec = find_by_cli_name(arg);
            if (spec == nullptr) {
                throw std::runtime_error("unknown argument: " + std::string(arg));
            }
            std::string value;
            if (spec->type == ValueType::Bool) {
                value = arg == spec->negative_cli_name ? "false" : "true";
            } else {
                value = std::string(need_value(index, arg));
            }
            named_assignments.push_back({.path = spec->path, .value = std::move(value)});
        }
    }

    for (const auto& path : config_files) {
        const std::unordered_set<std::string> assigned_paths = apply_file(path);
        result.assigned_paths.insert(assigned_paths.begin(), assigned_paths.end());
    }

    for (const NamedAssignment& assignment : named_assignments) {
        set(std::string_view(assignment.path), std::string_view(assignment.value));
        result.assigned_paths.insert(assignment.path);
    }

    for (const std::string& assignment : deferred_sets) {
        const std::size_t separator = assignment.find('=');
        if (separator == std::string::npos || separator == 0U) {
            throw std::runtime_error("--set expects path=value");
        }
        set(std::string_view(assignment.data(), separator),
            std::string_view(assignment.data() + separator + 1U,
                             assignment.size() - separator - 1U));
        result.assigned_paths.insert(std::string(assignment.data(), separator));
    }
    return result;
}

nlohmann::json Schema::template_json() const {
    nlohmann::json document = nlohmann::json::object();
    for (const Binding& binding : bindings_) {
        set_json_path(document, binding.spec.path, binding.get_json());
    }
    return document;
}

void Schema::write_template(const std::filesystem::path& path) const {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("could not write config template: " + path.string());
    }
    output << template_json().dump(2) << '\n';
}

void Schema::validate_specs(std::span<const Binding> bindings) {
    std::unordered_set<std::string> paths;
    std::unordered_set<std::string> aliases;
    for (const Binding& binding : bindings) {
        const OptionSpec& spec = binding.spec;
        if (spec.label.empty() || spec.group_path.empty() || spec.help.empty()) {
            throw std::runtime_error(
                "config option metadata must include label, group, and help: " + spec.path);
        }
        if (!valid_path(spec.path)) {
            throw std::runtime_error("config option path must contain nonempty components");
        }
        if (spec.cli_name.size() <= 2U || spec.cli_name.rfind("--", 0) != 0U) {
            throw std::runtime_error("config option CLI name must start with --");
        }
        if (is_bootstrap_alias(spec.cli_name) ||
            (!spec.negative_cli_name.empty() && is_bootstrap_alias(spec.negative_cli_name))) {
            throw std::runtime_error("config option CLI alias is reserved for bootstrap parsing: " +
                                     spec.path);
        }
        if (!spec.negative_cli_name.empty() &&
            (spec.type != ValueType::Bool || spec.negative_cli_name.size() <= 2U ||
             spec.negative_cli_name.rfind("--", 0) != 0U)) {
            throw std::runtime_error(
                "negative config aliases must be named --... and belong to bool options");
        }
        if (!paths.insert(spec.path).second) {
            throw std::runtime_error("duplicate config option path: " + spec.path);
        }
        if (!aliases.insert(spec.cli_name).second) {
            throw std::runtime_error("duplicate config option CLI alias: " + spec.cli_name);
        }
        if (!spec.negative_cli_name.empty() && !aliases.insert(spec.negative_cli_name).second) {
            throw std::runtime_error("duplicate config option CLI alias: " +
                                     spec.negative_cli_name);
        }
        if ((spec.range.has_min && !std::isfinite(spec.range.min)) ||
            (spec.range.has_max && !std::isfinite(spec.range.max)) ||
            (spec.range.has_min && spec.range.has_max && spec.range.min > spec.range.max)) {
            throw std::runtime_error("invalid range metadata for config option: " + spec.path);
        }
        if (!spec.range.has_min && !spec.range.has_max) {
            // no-op: an absent range is valid for every primitive type
        } else if (spec.type == ValueType::Bool || spec.type == ValueType::String ||
                   spec.type == ValueType::Path || spec.type == ValueType::Enum) {
            throw std::runtime_error("range metadata is only valid for numeric config options: " +
                                     spec.path);
        }
        if (spec.type == ValueType::Enum) {
            if (spec.enum_values.empty()) {
                throw std::runtime_error("enum config option has no choices: " + spec.path);
            }
            std::unordered_set<std::string> choices;
            for (const std::string& choice : spec.enum_values) {
                if (choice.empty() || !choices.insert(choice).second) {
                    throw std::runtime_error("invalid enum metadata for config option: " +
                                             spec.path);
                }
            }
        } else if (!spec.enum_values.empty()) {
            throw std::runtime_error("non-enum config option has enum choices: " + spec.path);
        }
        if (binding.storage_type.has_value() &&
            !storage_type_matches(spec.type, binding.storage_type.value())) {
            throw std::runtime_error(
                "config option binding type does not match its specification: " + spec.path);
        }
        if ((spec.type == ValueType::UInt32 || spec.type == ValueType::UInt64) &&
            spec.range.has_min && spec.range.min < 0.0) {
            throw std::runtime_error("unsigned config option has a negative minimum: " + spec.path);
        }
    }
    for (std::size_t index = 0; index < bindings.size(); ++index) {
        const std::string& first = bindings[index].spec.path;
        for (std::size_t other = index + 1U; other < bindings.size(); ++other) {
            const std::string& second = bindings[other].spec.path;
            const bool first_prefix = second.rfind(first + ".", 0U) == 0U;
            const bool second_prefix = first.rfind(second + ".", 0U) == 0U;
            if (first_prefix || second_prefix) {
                throw std::runtime_error("config option paths conflict: " + first + " and " +
                                         second);
            }
        }
    }
}

Schema::Builder& Schema::Builder::compose(const Schema& schema) {
    bindings_.insert(bindings_.end(), schema.bindings_.begin(), schema.bindings_.end());
    return *this;
}

Schema::Builder& Schema::Builder::bind_custom(OptionSpec spec,
                                              std::function<void(std::string_view)> set_text,
                                              std::function<void(const nlohmann::json&)> set_json,
                                              std::function<nlohmann::json()> get_json) {
    if (!set_text || !set_json || !get_json) {
        throw std::runtime_error("custom config binding requires all operations");
    }
    bindings_.push_back({.spec = std::move(spec),
                         .set_text = std::move(set_text),
                         .set_json = std::move(set_json),
                         .get_json = std::move(get_json),
                         .storage_type = std::nullopt});
    return *this;
}

Schema Schema::Builder::build() && {
    return Schema(std::move(bindings_));
}

Schema Schema::Builder::build() const& {
    return Schema(bindings_);
}

Value Schema::Builder::parse_text(std::string_view value, const OptionSpec& spec) {
    switch (spec.type) {
    case ValueType::Bool:
        return parse_bool(value, spec);
    case ValueType::Int: {
        const std::int64_t parsed = parse_integer<std::int64_t>(value, spec, "integer");
        validate_number(static_cast<double>(parsed), spec);
        return parsed;
    }
    case ValueType::UInt32: {
        const std::uint64_t parsed = parse_integer<std::uint64_t>(value, spec, "unsigned integer");
        if (parsed > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("unsigned integer is outside uint32 range for " + spec.path);
        }
        validate_number(static_cast<double>(parsed), spec);
        return parsed;
    }
    case ValueType::UInt64: {
        const std::uint64_t parsed = parse_integer<std::uint64_t>(value, spec, "unsigned integer");
        validate_number(static_cast<double>(parsed), spec);
        return parsed;
    }
    case ValueType::Float: {
        const double parsed = parse_float(value, spec);
        validate_number(parsed, spec);
        return parsed;
    }
    case ValueType::String:
        return std::string(value);
    case ValueType::Path:
        return std::filesystem::path(std::string(value));
    case ValueType::Enum:
        if (std::find(spec.enum_values.begin(), spec.enum_values.end(), value) ==
            spec.enum_values.end()) {
            throw std::runtime_error("invalid enum value for " + spec.path);
        }
        return std::string(value);
    }
    throw std::runtime_error("unsupported config option type for " + spec.path);
}

Value Schema::Builder::parse_json(const nlohmann::json& value, const OptionSpec& spec) {
    if (value.is_null()) {
        throw std::runtime_error("null config values are no-ops");
    }
    if (!json_type_matches(value, spec)) {
        throw std::runtime_error("wrong JSON type for config option: " + spec.path);
    }
    switch (spec.type) {
    case ValueType::Bool:
        return value.get<bool>();
    case ValueType::Int: {
        const std::int64_t parsed = value.get<std::int64_t>();
        validate_number(static_cast<double>(parsed), spec);
        return parsed;
    }
    case ValueType::UInt32: {
        const std::uint64_t parsed = value.get<std::uint64_t>();
        if (parsed > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("unsigned integer is outside uint32 range for " + spec.path);
        }
        validate_number(static_cast<double>(parsed), spec);
        return parsed;
    }
    case ValueType::UInt64: {
        const std::uint64_t parsed = value.get<std::uint64_t>();
        validate_number(static_cast<double>(parsed), spec);
        return parsed;
    }
    case ValueType::Float: {
        const double parsed = value.get<double>();
        if (!std::isfinite(parsed)) {
            throw std::runtime_error("invalid float for " + spec.path);
        }
        validate_number(parsed, spec);
        return parsed;
    }
    case ValueType::String:
    case ValueType::Path:
    case ValueType::Enum:
        return parse_text(value.get<std::string>(), spec);
    }
    throw std::runtime_error("unsupported config option type for " + spec.path);
}

} // namespace cubey::config
