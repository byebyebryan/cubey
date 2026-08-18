#include <cubey/core/config_schema.h>
#include <cubey/host/common_config.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_throws(auto&& function, const char* message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

cubey::config::OptionSpec spec(std::string path, std::string cli, cubey::config::ValueType type,
                               cubey::config::Range range = {},
                               std::vector<std::string> choices = {}) {
    return {.path = std::move(path),
            .cli_name = std::move(cli),
            .negative_cli_name = {},
            .label = "Test option",
            .group_path = "Test",
            .help = "Test config option.",
            .type = type,
            .range = range,
            .enum_values = std::move(choices)};
}

struct Args {
    std::vector<std::string> values;
    std::vector<char*> pointers;

    explicit Args(std::initializer_list<std::string> arguments) : values{"test"} {
        values.insert(values.end(), arguments.begin(), arguments.end());
        pointers.reserve(values.size());
        for (std::string& value : values) {
            pointers.push_back(value.data());
        }
    }
};

} // namespace

void test_config_schema_composes_typed_bindings_and_emits_template() {
    std::optional<std::uint32_t> width;
    std::optional<std::string> mode;
    std::optional<float> ratio;
    std::optional<bool> enabled;
    const cubey::config::Schema first =
        cubey::config::Schema::builder()
            .bind(spec("render.width", "--render-width", cubey::config::ValueType::UInt32,
                       {.has_min = true, .min = 1.0}),
                  width)
            .bind(spec("render.mode", "--render-mode", cubey::config::ValueType::Enum, {},
                       {"draft", "final"}),
                  mode)
            .build();
    const cubey::config::Schema second =
        cubey::config::Schema::builder()
            .bind(spec("render.ratio", "--render-ratio", cubey::config::ValueType::Float,
                       {.has_min = true, .has_max = true, .min = 0.0, .max = 1.0}),
                  ratio)
            .bind(spec("render.enabled", "--render-enabled", cubey::config::ValueType::Bool),
                  enabled)
            .build();
    const cubey::config::Schema schema =
        cubey::config::Schema::builder().compose(first).compose(second).build();
    Args args({"--render-width", "64", "--render-mode", "final", "--render-ratio", "0.5",
               "--render-enabled"});
    static_cast<void>(
        schema.parse_cli(static_cast<int>(args.pointers.size()), args.pointers.data()));
    require(width == 64U && mode == "final" && ratio == 0.5F && enabled == true,
            "composed schema should parse into typed member bindings");
    const nlohmann::json document = schema.template_json();
    require(document.at("render").at("width") == 64U,
            "schema template should emit nested typed values");
    require(document.at("render").at("mode") == "final", "schema template should emit enum text");
}

void test_config_schema_binds_typed_enum_and_emits_symbolic_value() {
    enum class Quality {
        Draft,
        Final,
    };
    std::optional<Quality> quality;
    const cubey::config::Schema schema =
        cubey::config::Schema::builder()
            .bind(spec("render.quality", "--render-quality", cubey::config::ValueType::Enum, {},
                       {"draft", "final"}),
                  quality)
            .build();
    Args args({"--render-quality", "final"});
    static_cast<void>(
        schema.parse_cli(static_cast<int>(args.pointers.size()), args.pointers.data()));
    require(quality == Quality::Final, "typed enum binding should parse into enum storage");
    require(schema.template_json().at("render").at("quality") == "final",
            "typed enum binding should emit its symbolic choice");
}

void test_config_schema_rejects_duplicate_and_invalid_metadata() {
    std::optional<int> first;
    std::optional<int> second;
    std::optional<std::string> mode;
    require_throws(
        [&] {
            static_cast<void>(
                cubey::config::Schema::builder()
                    .bind(spec("same", "--one", cubey::config::ValueType::Int), first)
                    .bind(spec("same", "--two", cubey::config::ValueType::Int), second)
                    .build());
        },
        "schema should reject duplicate paths");
    require_throws(
        [&] {
            static_cast<void>(
                cubey::config::Schema::builder()
                    .bind(spec("one", "--same", cubey::config::ValueType::Int), first)
                    .bind(spec("two", "--same", cubey::config::ValueType::Int), second)
                    .build());
        },
        "schema should reject duplicate CLI aliases");
    require_throws(
        [&] {
            static_cast<void>(
                cubey::config::Schema::builder()
                    .bind(spec("mode", "--mode", cubey::config::ValueType::Enum), mode)
                    .build());
        },
        "schema should reject enum metadata without choices");
    {
        cubey::config::OptionSpec missing_help =
            spec("missing_help", "--missing-help", cubey::config::ValueType::Int);
        missing_help.help.clear();
        require_throws(
            [&] {
                static_cast<void>(
                    cubey::config::Schema::builder().bind(std::move(missing_help), first).build());
            },
            "schema should reject incomplete option metadata");
    }
    {
        require_throws(
            [&] {
                static_cast<void>(
                    cubey::config::Schema::builder()
                        .bind(spec("mismatch", "--mismatch", cubey::config::ValueType::Bool), first)
                        .build());
            },
            "schema should reject binding/storage type mismatches");
    }
    {
        cubey::config::OptionSpec invalid_bool_alias =
            spec("enabled", "--enabled", cubey::config::ValueType::Bool);
        invalid_bool_alias.negative_cli_name = "no-enabled";
        require_throws(
            [&] {
                static_cast<void>(cubey::config::Schema::builder()
                                      .bind(std::move(invalid_bool_alias), first)
                                      .build());
            },
            "schema should reject malformed bool negative aliases");
    }
    {
        require_throws(
            [&] {
                static_cast<void>(
                    cubey::config::Schema::builder()
                        .bind(spec("reserved", "--config", cubey::config::ValueType::String), mode)
                        .build());
            },
            "schema should reject positive aliases reserved for bootstrap parsing");
    }
    {
        cubey::config::OptionSpec reserved_negative =
            spec("reserved_negative", "--reserved-negative", cubey::config::ValueType::Bool);
        reserved_negative.negative_cli_name = "--set";
        std::optional<bool> reserved_negative_storage;
        require_throws(
            [&] {
                static_cast<void>(cubey::config::Schema::builder()
                                      .bind(std::move(reserved_negative), reserved_negative_storage)
                                      .build());
            },
            "schema should reject negative aliases reserved for bootstrap parsing");
    }
}

void test_config_schema_rejects_wrong_type_unknown_and_range() {
    std::optional<std::uint32_t> width;
    std::optional<std::string> mode;
    const cubey::config::Schema schema =
        cubey::config::Schema::builder()
            .bind(spec("width", "--width", cubey::config::ValueType::UInt32,
                       {.has_min = true, .has_max = true, .min = 1.0, .max = 100.0}),
                  width)
            .bind(spec("mode", "--mode", cubey::config::ValueType::Enum, {}, {"a", "b"}), mode)
            .build();
    require_throws([&] { schema.apply_json({{"width", "wrong"}}); },
                   "schema should reject wrong JSON type");
    require_throws([&] { schema.apply_json({{"unknown", 1}}); },
                   "schema should reject unknown JSON key");
    require_throws([&] { schema.apply_json({{"width", 101}}); },
                   "schema should reject JSON range violations");
    require_throws([&] { schema.set("mode", std::string_view{"invalid"}); },
                   "schema should reject invalid enum text");
}

void test_config_schema_bool_aliases_null_and_precedence() {
    std::optional<bool> enabled;
    std::optional<std::uint32_t> value;
    cubey::config::OptionSpec enabled_spec =
        spec("enabled", "--enabled", cubey::config::ValueType::Bool);
    enabled_spec.negative_cli_name = "--no-enabled";
    const cubey::config::Schema schema =
        cubey::config::Schema::builder()
            .bind(std::move(enabled_spec), enabled)
            .bind(spec("value", "--value", cubey::config::ValueType::UInt32), value)
            .build();
    enabled = true;
    schema.apply_json({{"enabled", nullptr}});
    require(enabled == true, "JSON null should be a no-op");
    Args args({"--enabled", "--no-enabled", "--value", "1", "--set", "value=2"});
    static_cast<void>(
        schema.parse_cli(static_cast<int>(args.pointers.size()), args.pointers.data()));
    require(enabled == false, "negative bool alias should set false");
    require(value == 2U, "deferred --set should override named CLI values");
}

void test_config_schema_layers_config_files_in_argv_order() {
    std::optional<std::string> mode;
    const cubey::config::Schema schema =
        cubey::config::Schema::builder()
            .bind(spec("mode", "--mode", cubey::config::ValueType::Enum, {}, {"a", "b", "c"}), mode)
            .build();
    const auto first = std::filesystem::temp_directory_path() / "cubey-schema-first.json";
    const auto second = std::filesystem::temp_directory_path() / "cubey-schema-second.json";
    {
        std::ofstream output(first);
        output << R"({"mode":"a"})";
    }
    {
        std::ofstream output(second);
        output << R"({"mode":"b"})";
    }
    Args args({"--config", first.string(), "--config", second.string(), "--mode", "c"});
    static_cast<void>(
        schema.parse_cli(static_cast<int>(args.pointers.size()), args.pointers.data()));
    require(mode == "c", "named CLI should override config files in argv order");
    std::filesystem::remove(first);
    std::filesystem::remove(second);
}

void test_config_schema_named_values_can_spell_bootstrap_flags() {
    std::optional<std::string> text;
    std::optional<std::filesystem::path> path;
    const cubey::config::Schema schema =
        cubey::config::Schema::builder()
            .bind(spec("text", "--text", cubey::config::ValueType::String), text)
            .bind(spec("path", "--path", cubey::config::ValueType::Path), path)
            .build();
    Args args({"--text", "--config", "--path", "--write-config-template"});
    static_cast<void>(
        schema.parse_cli(static_cast<int>(args.pointers.size()), args.pointers.data()));
    require(text == "--config", "named string values should consume bootstrap-looking tokens");
    require(path == std::filesystem::path("--write-config-template"),
            "named path values should consume bootstrap-looking tokens");
}

void test_common_host_config_normalizes_capture_compatibility() {
    {
        Args args({"--headless", "--capture", "video"});
        const cubey::host::CommonRunConfig config = cubey::host::parse_common_run_config(
            static_cast<int>(args.pointers.size()), args.pointers.data());
        require(config.frames == 300U, "video capture should default to 300 frames");
        require(config.output_path == "cubey-output.mp4",
                "video capture should default to the compatible mp4 path");
    }
    {
        Args args({"--require-validation", "--no-validation"});
        const cubey::host::CommonRunConfig config = cubey::host::parse_common_run_config(
            static_cast<int>(args.pointers.size()), args.pointers.data());
        require(!config.validation && !config.require_validation,
                "later validation disable should clear the requirement");
    }
    {
        Args args({"--capture", "video"});
        require_throws(
            [&] {
                static_cast<void>(cubey::host::parse_common_run_config(
                    static_cast<int>(args.pointers.size()), args.pointers.data()));
            },
            "video capture should require headless mode");
    }
    {
        Args args({"--headless", "--capture", "video", "--output", "cubey-output.png"});
        const cubey::host::CommonRunConfig config = cubey::host::parse_common_run_config(
            static_cast<int>(args.pointers.size()), args.pointers.data());
        require(config.output_path == "cubey-output.png",
                "an explicit default output path should not be replaced by video normalization");
    }
}
