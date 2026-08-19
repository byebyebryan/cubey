#pragma once

#include <cubey/core/config_schema.h>
#include <cubey/host/common_config.h>

#include <cstdio>
#include <exception>
#include <type_traits>
#include <utility>

namespace cubey::host {

struct ConfiguredAppInfo {
    const char* app_name = "cubey";
    const char* default_title = "cubey";
};

// Shared startup control flow for project-owned typed facades. The parser
// owns the schema and validation; this helper only handles the common host
// normalization, template early-exit, default title, and error convention.
template <typename Config, typename SchemaFactory>
Config parse_configured_app(int argc, char** argv, SchemaFactory&& schema_factory,
                            config::ParseResult* result = nullptr) {
    Config config;
    config::Schema schema = std::forward<SchemaFactory>(schema_factory)(config);
    config::ParseResult parsed = schema.parse_cli(argc, argv);
    normalize_common_run_config(config.common, parsed.path_was_assigned("output") ||
                                                   config.common.output_path != "cubey-output.png");
    if (parsed.write_config_template_path.has_value()) {
        schema.write_template(parsed.write_config_template_path.value());
    }
    if (result != nullptr) {
        *result = parsed;
    }
    return config;
}

template <typename ParseFn, typename RunFn>
int run_configured_app(int argc, char** argv, ConfiguredAppInfo info, ParseFn&& parse,
                       RunFn&& run) {
    try {
        config::ParseResult result;
        using Config =
            std::decay_t<std::invoke_result_t<ParseFn&, int, char**, config::ParseResult*>>;
        Config config = std::forward<ParseFn>(parse)(argc, argv, &result);
        if (result.write_config_template_path.has_value()) {
            return 0;
        }
        if (config.common.title == "cubey") {
            config.common.title = info.default_title;
        }
        return std::forward<RunFn>(run)(config);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s: %s\n", info.app_name, error.what());
        return 1;
    }
}

} // namespace cubey::host
