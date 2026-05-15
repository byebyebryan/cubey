#include "source_file_test_helpers.h"

#include <filesystem>
#include <initializer_list>
#include <string>

namespace {

using cubey::tests::read_source_file;
using cubey::tests::require_contains;
using cubey::tests::require_not_contains;

std::string read_example_sources(const std::filesystem::path& root,
                                 std::initializer_list<const char*> paths) {
    std::string result;
    for (const char* path : paths) {
        result += read_source_file(root / path);
    }
    return result;
}

} // namespace

void test_cube_examples_share_spinning_cube_motion() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string common = read_source_file(root / "examples/common/cube_scene.h");
    const std::string spinning = read_example_sources(
        root, {"examples/spinning_cube/spinning_cube_app_internal.h",
               "examples/spinning_cube/spinning_cube_resources.cpp",
               "examples/spinning_cube/spinning_cube_scene.cpp",
               "examples/spinning_cube/spinning_cube_render.cpp"});
    const std::string textured = read_example_sources(
        root, {"examples/textured_cube/textured_cube_app_internal.h",
               "examples/textured_cube/textured_cube_resources.cpp",
               "examples/textured_cube/textured_cube_scene.cpp",
               "examples/textured_cube/textured_cube_render.cpp"});
    const std::string instanced = read_example_sources(
        root, {"examples/instanced_cubes/instanced_cubes_app_internal.h",
               "examples/instanced_cubes/instanced_cubes_resources.cpp",
               "examples/instanced_cubes/instanced_cubes_scene.cpp",
               "examples/instanced_cubes/instanced_cubes_render.cpp"});
    const std::string instanced_shader =
        read_source_file(root / "examples/instanced_cubes/shaders/instanced_cubes.vert");
    const std::string material_scene =
        read_source_file(root / "examples/material_cubes/material_cubes_scene.cpp");
    const std::string material =
        read_source_file(root / "examples/material_cubes/material_cubes_app_internal.h") +
        read_source_file(root / "examples/material_cubes/material_cubes_resources.cpp") +
        read_source_file(root / "examples/material_cubes/material_cubes_render.cpp") +
        material_scene;
    const std::string shadow_scene =
        read_source_file(root / "examples/shadow_cube/shadow_cube_scene.cpp");
    const std::string shadow_app =
        read_source_file(root / "examples/shadow_cube/shadow_cube_app.cpp");
    const std::string particle_app = read_example_sources(
        root, {"examples/particle_cubes/particle_cubes_app.cpp",
               "examples/particle_cubes/particle_cubes_app_internal.h",
               "examples/particle_cubes/particle_cubes_resources.cpp",
               "examples/particle_cubes/particle_cubes_simulation.cpp",
               "examples/particle_cubes/particle_cubes_render.cpp"});
    const std::string particle_shader =
        read_source_file(root / "examples/particle_cubes/shaders/particle_cubes.vert");

    require_contains(common, "cube_spin_rotation",
                     "cube examples should share the spinning-cube rotation helper");
    require_contains(common, "0.55F", "common cube spin should preserve spinning_cube pitch speed");
    require_contains(common, "0.9F", "common cube spin should preserve spinning_cube yaw speed");

    for (const std::string* source : {&spinning, &textured, &material_scene, &shadow_scene}) {
        require_contains(*source, "cubey::examples::common::cube_spin_transform",
                         "scene-backed cube examples should use the common spin transform");
    }
    require_contains(instanced, "cube_spin_transform(seconds).affine_matrix()",
                     "instanced cubes should build per-cube spin from the common helper");
    require_contains(instanced, ".cube_spin = cube_spin",
                     "instanced cubes should pass common spin as per-cube local motion");
    require_contains(instanced_shader, "model * pc.cube_spin",
                     "instanced cube grid should stay static while cubes spin locally");
    require_contains(instanced, "constexpr float kGridSpacing = 1.28F",
                     "instanced cube grid should use tighter furnace-style spacing");
    require_contains(instanced, "const float y = (2.0F - static_cast<float>(row)) * kGridSpacing;",
                     "instanced cube rows should map to vertical screen space");
    require_contains(instanced, "cubey::math::translation(x, y, 0.0F)",
                     "instanced cube grid should be front-facing instead of side-on");
    require_contains(instanced, ".distance = kCameraDistance",
                     "instanced cube camera should use shared front-facing framing distance");
    require_not_contains(instanced, "const float z = (static_cast<float>(row)",
                         "instanced cube rows should not be laid out in depth");
    require_not_contains(instanced, "set_auto_rotation_speed",
                         "instanced cube camera should not auto-orbit the static grid");
    require_contains(material_scene, "cubey::math::Vec3 material_cube_translation",
                     "material cube grid should keep explicit front-facing cell placement");
    require_contains(material_scene, "0.0F,",
                     "material cube grid should keep cells on the front-facing XY plane");
    require_contains(material, ".distance = kCameraDistance",
                     "material cube camera should use shared front-facing framing distance");
    require_not_contains(material, "set_auto_rotation_speed",
                         "material cube camera should not auto-orbit the static grid");
    require_contains(shadow_app, "update_scene_transform(timing)",
                     "shadow cube should update cube motion from frame timing");

    require_contains(particle_app, "OrbitController orbit_controller_",
                     "particle cubes should expose shared orbit camera control");
    require_contains(particle_app, "orbit_controller_.update_from_input",
                     "particle cubes should drive camera control from the input layer");
    require_contains(particle_app, "kParticleCubeCount = 4096",
                     "particle cubes should render a 4k cube field");
    require_contains(particle_app, "kParticleCubeMinScale = 0.01F",
                     "particle cubes should use the requested minimum cube scale");
    require_contains(particle_app, "kParticleCubeScaleRange = 0.03F",
                     "particle cubes should use the requested maximum cube scale");
    require_not_contains(particle_app, "orbit_time",
                         "particle cubes should not auto-orbit after camera control is added");
    require_not_contains(particle_app, "spin_time",
                         "particle cubes should not pass a local cube spin time");
    require_not_contains(particle_shader, "cube_spin_rotation",
                         "particle cubes should not rotate individual cubes in the vertex shader");
}

void test_cube_examples_split_app_lifecycle_from_resources_scene_and_rendering() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};

    const std::string spinning_app =
        read_source_file(root / "examples/spinning_cube/spinning_cube_app.cpp");
    const std::string spinning_internal =
        read_source_file(root / "examples/spinning_cube/spinning_cube_app_internal.h");
    const std::string spinning_resources =
        read_source_file(root / "examples/spinning_cube/spinning_cube_resources.cpp");
    const std::string spinning_scene =
        read_source_file(root / "examples/spinning_cube/spinning_cube_scene.cpp");
    const std::string spinning_render =
        read_source_file(root / "examples/spinning_cube/spinning_cube_render.cpp");
    const std::string spinning_cmake =
        read_source_file(root / "examples/spinning_cube/CMakeLists.txt");

    require_contains(spinning_app, "#include \"spinning_cube_app_internal.h\"",
                     "spinning_cube app shell should include its internal split header");
    require_not_contains(spinning_app, "void create_forward_pass",
                         "spinning_cube app shell should not own forward pass setup");
    require_not_contains(spinning_app, "void record_cube_frame",
                         "spinning_cube app shell should not own frame recording");
    require_contains(spinning_internal, "class SpinningCubeApp",
                     "spinning_cube internal header should own private app state");
    require_contains(spinning_resources, "create_global_resources_if_needed",
                     "spinning_cube resources file should own resource creation");
    require_contains(spinning_scene, "create_scene",
                     "spinning_cube scene file should own scene construction");
    require_contains(spinning_render, "record_cube_frame",
                     "spinning_cube render file should own frame recording");
    require_contains(spinning_cmake, "spinning_cube_resources.cpp",
                     "spinning_cube target should build the resources file");
    require_contains(spinning_cmake, "spinning_cube_scene.cpp",
                     "spinning_cube target should build the scene file");
    require_contains(spinning_cmake, "spinning_cube_render.cpp",
                     "spinning_cube target should build the render file");

    const std::string textured_app =
        read_source_file(root / "examples/textured_cube/textured_cube_app.cpp");
    const std::string textured_sources = read_example_sources(
        root, {"examples/textured_cube/textured_cube_app_internal.h",
               "examples/textured_cube/textured_cube_resources.cpp",
               "examples/textured_cube/textured_cube_scene.cpp",
               "examples/textured_cube/textured_cube_render.cpp",
               "examples/textured_cube/CMakeLists.txt"});
    require_contains(textured_app, "#include \"textured_cube_app_internal.h\"",
                     "textured_cube app shell should include its internal split header");
    require_not_contains(textured_app, "void create_texture_resources",
                         "textured_cube app shell should not own generated texture resources");
    require_contains(textured_sources, "create_texture_resources",
                     "textured_cube resources file should own generated texture setup");
    require_contains(textured_sources, "textured_cube_resources.cpp",
                     "textured_cube target should build the resources file");
    require_contains(textured_sources, "textured_cube_scene.cpp",
                     "textured_cube target should build the scene file");
    require_contains(textured_sources, "textured_cube_render.cpp",
                     "textured_cube target should build the render file");

    const std::string instanced_app =
        read_source_file(root / "examples/instanced_cubes/instanced_cubes_app.cpp");
    const std::string instanced_sources = read_example_sources(
        root, {"examples/instanced_cubes/instanced_cubes_app_internal.h",
               "examples/instanced_cubes/instanced_cubes_resources.cpp",
               "examples/instanced_cubes/instanced_cubes_scene.cpp",
               "examples/instanced_cubes/instanced_cubes_render.cpp",
               "examples/instanced_cubes/CMakeLists.txt"});
    require_contains(instanced_app, "#include \"instanced_cubes_app_internal.h\"",
                     "instanced_cubes app shell should include its internal split header");
    require_not_contains(instanced_app, "void create_forward_pass",
                         "instanced_cubes app shell should not own forward pass setup");
    require_contains(instanced_sources, "create_instance_data",
                     "instanced_cubes resources file should keep instance setup discoverable");
    require_contains(instanced_sources, "instanced_cubes_resources.cpp",
                     "instanced_cubes target should build the resources file");
    require_contains(instanced_sources, "instanced_cubes_scene.cpp",
                     "instanced_cubes target should build the scene file");
    require_contains(instanced_sources, "instanced_cubes_render.cpp",
                     "instanced_cubes target should build the render file");

    const std::string particle_app =
        read_source_file(root / "examples/particle_cubes/particle_cubes_app.cpp");
    const std::string particle_sources = read_example_sources(
        root, {"examples/particle_cubes/particle_cubes_app_internal.h",
               "examples/particle_cubes/particle_cubes_resources.cpp",
               "examples/particle_cubes/particle_cubes_simulation.cpp",
               "examples/particle_cubes/particle_cubes_render.cpp",
               "examples/particle_cubes/CMakeLists.txt"});
    require_contains(particle_app, "#include \"particle_cubes_app_internal.h\"",
                     "particle_cubes app shell should include its internal split header");
    require_not_contains(particle_app, "void record_particle_compute",
                         "particle_cubes app shell should not own compute recording");
    require_contains(particle_sources, "record_particle_compute",
                     "particle_cubes simulation file should own compute recording");
    require_contains(particle_sources, "particle_cubes_resources.cpp",
                     "particle_cubes target should build the resources file");
    require_contains(particle_sources, "particle_cubes_simulation.cpp",
                     "particle_cubes target should build the simulation file");
    require_contains(particle_sources, "particle_cubes_render.cpp",
                     "particle_cubes target should build the render file");

    const std::string headless_app =
        read_source_file(root / "examples/headless_cube/headless_cube_app.cpp");
    const std::string headless_sources = read_example_sources(
        root, {"examples/headless_cube/headless_cube_app_internal.h",
               "examples/headless_cube/headless_cube_resources.cpp",
               "examples/headless_cube/headless_cube_render.cpp",
               "examples/headless_cube/CMakeLists.txt"});
    require_contains(headless_app, "#include \"headless_cube_app_internal.h\"",
                     "headless_cube app shell should include its internal split header");
    require_not_contains(headless_app, "void render_png",
                         "headless_cube app shell should not own render recording");
    require_contains(headless_sources, "create_global_resources_if_needed",
                     "headless_cube resources file should own resource creation");
    require_contains(headless_sources, "render_png",
                     "headless_cube render file should own png rendering");
    require_contains(headless_sources, "headless_cube_resources.cpp",
                     "headless_cube target should build the resources file");
    require_contains(headless_sources, "headless_cube_render.cpp",
                     "headless_cube target should build the render file");
}

void test_shadow_cube_ground_plane_sits_below_spinning_cube() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string resources =
        read_source_file(root / "examples/shadow_cube/shadow_cube_resources.cpp");
    const std::string scene = read_source_file(root / "examples/shadow_cube/shadow_cube_scene.cpp");
    const std::string app = read_source_file(root / "examples/shadow_cube/shadow_cube_app.cpp");
    const std::string header =
        read_source_file(root / "examples/shadow_cube/shadow_cube_app_internal.h");

    require_contains(header, "kShadowCubeGroundPlaneY = -1.5F",
                     "shadow_cube ground plane should sit below the rotating cube");
    require_contains(header, "floor_entity_", "shadow_cube should keep a ground plane entity");
    require_contains(header, "floor_mesh_handle_",
                     "shadow_cube should keep a ground plane mesh handle");
    require_contains(resources, "shadow_cube.floor",
                     "shadow_cube should create a named ground plane mesh");
    require_contains(resources, "make_xz_plane_position_color_normal_mesh",
                     "shadow_cube ground plane should use the primitive plane builder");
    require_contains(resources, "kShadowCubeGroundPlaneY",
                     "shadow_cube ground mesh should use the shared lowered plane height");
    require_contains(scene, "floor_entity_",
                     "shadow_cube scene should create a ground plane entity");
    require_contains(scene, "kShadowCubeGroundPlaneY",
                     "shadow_cube scene bounds should use the shared lowered plane height");
    require_contains(app, "floor_entity_ = {};",
                     "shadow_cube should clear the ground entity on teardown");
}

void test_shadow_cube_transforms_normals_with_rotating_model_matrix() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string header = read_source_file(root / "examples/shadow_cube/shadow_cube_render.h");
    const std::string shader =
        read_source_file(root / "examples/shadow_cube/shaders/shadow_cube.vert");
    const std::string frame = read_source_file(root / "examples/shadow_cube/shadow_cube_frame.cpp");

    require_contains(header, "cubey::math::Mat4 model;",
                     "shadow_cube scene push constants should carry the model matrix");
    require_contains(frame, ".model = packet.world_affine_matrix",
                     "shadow_cube should push each packet model matrix");
    require_contains(shader, "transpose(inverse(mat3(push_constants.model)))",
                     "shadow_cube shader should transform normals by the model normal matrix");
}

void test_material_cubes_show_real_material_variant_grid() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string app =
        read_source_file(root / "examples/material_cubes/material_cubes_app_internal.h") +
        read_source_file(root / "examples/material_cubes/material_cubes_resources.cpp") +
        read_source_file(root / "examples/material_cubes/material_cubes_render.cpp");
    const std::string cmake = read_source_file(root / "examples/material_cubes/CMakeLists.txt");

    require_contains(app, "kMaterialGridColumns = 7",
                     "material_cubes should use a wider material-variant grid");
    require_contains(app, "kMaterialGridRows = 5",
                     "material_cubes should use a taller material-variant grid");
    require_contains(app, "kMaterialGridColumns * kMaterialGridRows",
                     "material cube count should derive from the explicit grid dimensions");
    require_contains(app, "struct MaterialVariant",
                     "material_cubes should name per-cell material factors explicitly");
    require_contains(app, "kNeutralMaterialBaseColor",
                     "material_cubes should keep base color neutral so PBR response is visible");
    require_contains(app, "roughness",
                     "material variants should include roughness, not only base color");
    require_contains(app, "metallic",
                     "material variants should include metallic, not only base color");
    require_contains(app, "material_variant_for_cell",
                     "material_cubes should generate material factors from grid cells");
    require_contains(app, "cubey::ForwardPbrRenderer3D",
                     "material_cubes should use the shared forward PBR renderer");
    require_contains(app, "forward_pbr_renderer().record({",
                     "material_cubes should submit direct PBR renderer frame requests");
    require_contains(app, "cubey::render::PbrMaterialFactors",
                     "material_cubes should store variants as PBR material factors");
    require_contains(app, "cubey::render::PbrVertex",
                     "material_cubes should use the PBR vertex layout");
    require_contains(app, "create_generated_pbr_environment",
                     "material_cubes should use generated IBL when no HDR environment is supplied");
    require_contains(app, "create_pbr_environment_from_equirectangular",
                     "material_cubes should support HDR IBL through the normal run config");
    require_contains(app, ".exposure = config_.exposure",
                     "material_cubes should use the PBR display transform settings");
    require_contains(app, ".environment_rotation_degrees = config_.environment_rotation_degrees",
                     "material_cubes should pass environment rotation to PBR rendering");
    require_not_contains(app, "struct MaterialUniforms",
                         "material_cubes should not keep the old custom material uniform path");

    require_contains(cmake, "cubey_forward_pbr_shader_sources",
                     "material_cubes should compile the shared forward PBR shader package");
    require_contains(cmake, "CUBEY_FORWARD_PBR_SHADERS",
                     "material_cubes should use the shared forward PBR shader source list");
    require_not_contains(cmake, "projects/gltf_viewer/shaders",
                         "material_cubes should not depend on glTF viewer project shaders");
    require_not_contains(cmake, "gltf_pbr",
                         "material_cubes should not reference old glTF-named PBR shaders");
}
