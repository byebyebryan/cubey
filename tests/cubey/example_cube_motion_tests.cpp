#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string read_source_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open source file: " + path.string());
    }
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

void require_contains(const std::string& text, const std::string& needle,
                      const char* message) {
    require(text.find(needle) != std::string::npos, message);
}

void require_not_contains(const std::string& text, const std::string& needle,
                          const char* message) {
    require(text.find(needle) == std::string::npos, message);
}

} // namespace

void test_cube_examples_share_spinning_cube_motion() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string common = read_source_file(root / "examples/common/cube_scene.h");
    const std::string spinning =
        read_source_file(root / "examples/spinning_cube/spinning_cube_app.cpp");
    const std::string textured =
        read_source_file(root / "examples/textured_cube/textured_cube_app.cpp");
    const std::string instanced =
        read_source_file(root / "examples/instanced_cubes/instanced_cubes_app.cpp");
    const std::string instanced_shader =
        read_source_file(root / "examples/instanced_cubes/shaders/instanced_cubes.vert");
    const std::string material =
        read_source_file(root / "examples/material_cubes/material_cubes_app.cpp");
    const std::string shadow_scene =
        read_source_file(root / "examples/shadow_cube/shadow_cube_scene.cpp");
    const std::string shadow_app =
        read_source_file(root / "examples/shadow_cube/shadow_cube_app.cpp");
    const std::string particle_app =
        read_source_file(root / "examples/particle_cubes/particle_cubes_app.cpp");
    const std::string particle_shader =
        read_source_file(root / "examples/particle_cubes/shaders/particle_cubes.vert");

    require_contains(common, "cube_spin_rotation",
                     "cube examples should share the spinning-cube rotation helper");
    require_contains(common, "0.55F",
                     "common cube spin should preserve spinning_cube pitch speed");
    require_contains(common, "0.9F",
                     "common cube spin should preserve spinning_cube yaw speed");

    for (const std::string* source : {&spinning, &textured, &material, &shadow_scene}) {
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
    require_contains(material, "cubey::math::Vec3 material_cube_translation",
                     "material cube grid should keep explicit front-facing cell placement");
    require_contains(material, "0.0F,",
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

void test_shadow_cube_ground_plane_sits_below_spinning_cube() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string resources =
        read_source_file(root / "examples/shadow_cube/shadow_cube_resources.cpp");
    const std::string scene =
        read_source_file(root / "examples/shadow_cube/shadow_cube_scene.cpp");
    const std::string app = read_source_file(root / "examples/shadow_cube/shadow_cube_app.cpp");
    const std::string header =
        read_source_file(root / "examples/shadow_cube/shadow_cube_app_internal.h");

    require_contains(header, "kShadowCubeGroundPlaneY = -1.5F",
                     "shadow_cube ground plane should sit below the rotating cube");
    require_contains(header, "floor_entity_",
                     "shadow_cube should keep a ground plane entity");
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
    const std::string header =
        read_source_file(root / "examples/shadow_cube/shadow_cube_render.h");
    const std::string shader =
        read_source_file(root / "examples/shadow_cube/shaders/shadow_cube.vert");
    const std::string frame =
        read_source_file(root / "examples/shadow_cube/shadow_cube_frame.cpp");

    require_contains(header, "cubey::math::Mat4 model;",
                     "shadow_cube scene push constants should carry the model matrix");
    require_contains(frame, ".model = packet.world_affine_matrix",
                     "shadow_cube should push each packet model matrix");
    require_contains(shader, "transpose(inverse(mat3(push_constants.model)))",
                     "shadow_cube shader should transform normals by the model normal matrix");
}

void test_material_cubes_show_real_material_variant_grid() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string app = read_source_file(root / "examples/material_cubes/material_cubes_app.cpp");
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
    require_contains(app, "ForwardPbrRenderer3DRenderRequest",
                     "material_cubes should submit through the PBR renderer request path");
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

    require_contains(cmake, "gltf_pbr.frag",
                     "material_cubes should compile the shared PBR material shader");
    require_contains(cmake, "pbr_post.frag",
                     "material_cubes should compile the PBR post/display transform shader");
    require_contains(cmake, "gltf_skybox.frag",
                     "material_cubes should compile the PBR skybox shader");
}
