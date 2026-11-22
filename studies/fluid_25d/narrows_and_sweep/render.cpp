#define GL_GLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <cubey/render/atmosphere_environment.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
constexpr int nx = 256;
constexpr int ny = 96;
constexpr int cells = nx * ny;
constexpr int snapshots = 361;
constexpr int frames = 360;
constexpr int fields_per_snapshot = 3; // Bed, depth, reserved.
constexpr double snapshot_seconds = 0.25;
constexpr double simulation_seconds_per_frame = 0.18;
constexpr glm::ivec2 reach_start{25, 8}, reach_end{218, 87};

enum class Surface : GLint {
    Bed = 0,
    Water,
    CutBed,
    CutWater
};

static_assert(sizeof(float) == 4 && std::endian::native == std::endian::little,
              "Narrows and Sweep trace requires little-endian float32");

std::string read_text(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("Could not read shader: " + path.string());
    return {std::istreambuf_iterator<char>{file}, {}};
}

GLuint make_shader(GLenum type, const std::string& source) {
    const GLuint shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        std::array<char, 4096> log{};
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
        throw std::runtime_error(log.data());
    }
    return shader;
}

GLuint make_program(const std::filesystem::path& shader_dir, const std::string& name) {
    const GLuint vertex = make_shader(GL_VERTEX_SHADER, read_text(shader_dir / (name + ".vert")));
    const GLuint fragment =
        make_shader(GL_FRAGMENT_SHADER, read_text(shader_dir / (name + ".frag")));
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        std::array<char, 4096> log{};
        glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), nullptr, log.data());
        throw std::runtime_error(log.data());
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

struct SoftwareContext {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;

    SoftwareContext() = default;
    SoftwareContext(const SoftwareContext&) = delete;
    SoftwareContext& operator=(const SoftwareContext&) = delete;

    void start(int width, int height) {
        display =
            eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr) ||
            !eglBindAPI(EGL_OPENGL_API)) {
            throw std::runtime_error("Could not initialize surfaceless EGL");
        }
        constexpr EGLint attributes[]{EGL_SURFACE_TYPE,
                                      EGL_PBUFFER_BIT,
                                      EGL_RENDERABLE_TYPE,
                                      EGL_OPENGL_BIT,
                                      EGL_RED_SIZE,
                                      8,
                                      EGL_GREEN_SIZE,
                                      8,
                                      EGL_BLUE_SIZE,
                                      8,
                                      EGL_DEPTH_SIZE,
                                      24,
                                      EGL_SAMPLE_BUFFERS,
                                      1,
                                      EGL_SAMPLES,
                                      4,
                                      EGL_NONE};
        EGLConfig config{};
        EGLint count = 0;
        if (!eglChooseConfig(display, attributes, &config, 1, &count) || count == 0) {
            throw std::runtime_error("EGL could not select a 4x multisample/depth configuration");
        }
        const EGLint surface_attributes[]{EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
        constexpr EGLint context_attributes[]{EGL_CONTEXT_MAJOR_VERSION, 3,
                                              EGL_CONTEXT_MINOR_VERSION, 3, EGL_NONE};
        surface = eglCreatePbufferSurface(display, config, surface_attributes);
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
        if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
            !eglMakeCurrent(display, surface, surface, context)) {
            throw std::runtime_error("EGL could not start the rasterizer");
        }
        const auto* raw_renderer = glGetString(GL_RENDERER);
        const std::string renderer =
            raw_renderer ? reinterpret_cast<const char*>(raw_renderer) : "";
        if (renderer.find("llvmpipe") == std::string::npos) {
            throw std::runtime_error("Narrows and Sweep capture requires Mesa llvmpipe: " +
                                     renderer);
        }
        std::cerr << renderer << '\n';
    }

    ~SoftwareContext() {
        if (display != EGL_NO_DISPLAY) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context != EGL_NO_CONTEXT) {
                eglDestroyContext(display, context);
            }
            if (surface != EGL_NO_SURFACE) {
                eglDestroySurface(display, surface);
            }
            eglTerminate(display);
        }
    }
};

GLuint make_texture(int unit, const float* values) {
    GLuint texture{};
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, nx, ny, 0, GL_RED, GL_FLOAT, values);
    return texture;
}

void upload(int unit, GLuint texture, const float* values) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, nx, ny, GL_RED, GL_FLOAT, values);
}

void upload_sky(const std::array<std::filesystem::path, 3>& plates, int width, int height) {
    GLuint texture{};
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_SRGB8, width, height, plates.size(), 0, GL_RGB,
                 GL_UNSIGNED_BYTE, nullptr);
    std::vector<char> pixels(static_cast<std::size_t>(width) * height * 3);
    for (int layer = 0; layer < static_cast<int>(plates.size()); ++layer) {
        std::ifstream file(plates[layer], std::ios::binary);
        if (!file.read(pixels.data(), static_cast<std::streamsize>(pixels.size())) ||
            file.peek() != std::char_traits<char>::eof()) {
            throw std::runtime_error("Expected an RGB24 sky plate at output size: " +
                                     plates[layer].string());
        }
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, width, height, 1, GL_RGB,
                        GL_UNSIGNED_BYTE, pixels.data());
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
}

struct Mesh {
    GLuint vao{};
    GLsizei indices{};
    void draw() const {
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indices, GL_UNSIGNED_INT, nullptr);
    }
};

Mesh make_mesh(const std::vector<glm::vec3>& points, const std::vector<GLuint>& indices) {
    GLuint vao{};
    std::array<GLuint, 2> buffers{};
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
    glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
    glBufferData(GL_ARRAY_BUFFER, points.size() * sizeof(glm::vec3), points.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), nullptr);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(),
                 GL_STATIC_DRAW);
    return {vao, static_cast<GLsizei>(indices.size())};
}

Mesh make_grid(glm::ivec2 start, glm::ivec2 end) {
    // Half-meter display tessellation samples the original one-meter data through linear textures.
    const GLuint gx = (end.x - start.x) * 2 + 1;
    const GLuint gy = (end.y - start.y) * 2 + 1;
    std::vector<glm::vec3> points;
    std::vector<GLuint> indices;
    points.reserve(gx * gy);
    indices.reserve((gx - 1) * (gy - 1) * 6);
    for (GLuint y = 0; y < gy; ++y) {
        for (GLuint x = 0; x < gx; ++x) {
            points.emplace_back(start.x + 0.5f * x, start.y + 0.5f * y, 1.0f);
        }
    }
    for (GLuint y = 0; y + 1 < gy; ++y) {
        for (GLuint x = 0; x + 1 < gx; ++x) {
            const GLuint a = y * gx + x;
            for (GLuint i : {a, a + 1, a + gx, a + 1, a + gx + 1, a + gx})
                indices.push_back(i);
        }
    }
    return make_mesh(points, indices);
}

Mesh make_cut_faces(glm::vec2 start, glm::vec2 end) {
    const std::array<glm::vec2, 5> corners{{
        start,
        {end.x, start.y},
        end,
        {start.x, end.y},
        start,
    }};
    std::vector<glm::vec3> points;
    std::vector<GLuint> indices;
    for (std::size_t edge = 0; edge + 1 < corners.size(); ++edge) {
        const int segments = static_cast<int>(2.f * glm::length(corners[edge + 1] - corners[edge]));
        for (int i = 0; i <= segments; ++i) {
            const glm::vec2 xy = glm::mix(corners[edge], corners[edge + 1], float(i) / segments);
            points.emplace_back(xy, 0.0f);
            points.emplace_back(xy, 1.0f);
            if (i > 0) {
                const GLuint a = static_cast<GLuint>(points.size()) - 4;
                for (GLuint id : {a, a + 1, a + 2, a + 1, a + 3, a + 2})
                    indices.push_back(id);
            }
        }
    }
    return make_mesh(points, indices);
}

void soften_cells(std::array<float, cells>& v) {
    // A fixed small reconstruction filter rounds cell-scale kinks without retiming the fields.
    auto in = v;
    for (int y = 1; y + 1 < ny; ++y) {
        for (int x = 1; x + 1 < nx; ++x) {
            const int i = y * nx + x;
            v[i] = in[i] * 0.76f + 0.06f * (in[i - 1] + in[i + 1] + in[i - nx] + in[i + nx]);
        }
    }
}

int integer_argument(std::string_view text, int minimum, int maximum) {
    int value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < minimum ||
        value > maximum) {
        throw std::runtime_error("Capture argument is out of range: " + std::string(text));
    }
    return value;
}

std::vector<float> read_trace(const std::filesystem::path& path) {
    std::vector<float> trace(snapshots * fields_per_snapshot * cells);
    const auto bytes = static_cast<std::streamsize>(trace.size() * sizeof(float));
    std::ifstream input(path, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(trace.data()), bytes) ||
        input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Unexpected Narrows and Sweep trace size: " + path.string());
    }
    if (!std::ranges::all_of(trace, [](float value) { return std::isfinite(value); })) {
        throw std::runtime_error("Trace contains a nonfinite elevation or depth");
    }
    return trace;
}

std::array<float, cells> channel_floor(const std::vector<float>& trace) {
    std::array<float, cells> result{};
    for (int x = 0; x < nx; ++x) {
        float bottom = trace[x];
        for (int y = 1; y < ny; ++y) {
            bottom = std::min(bottom, trace[y * nx + x]);
        }
        for (int y = 0; y < ny; ++y) {
            result[y * nx + x] = bottom;
        }
    }
    return result;
}

std::array<float, cells> starting_surface(const std::vector<float>& trace) {
    std::array<float, cells> elevations{};
    for (int x = 0; x < nx; ++x) {
        float elevation = trace[x];
        bool wet = false;
        for (int y = 0; y < ny; ++y) {
            const int index = y * nx + x;
            if (trace[cells + index] > 0.04F) {
                const float height = trace[index] + trace[cells + index];
                elevation = wet ? std::max(elevation, height) : height;
                wet = true;
            }
        }
        for (int y = 0; y < ny; ++y) {
            elevations[y * nx + x] = elevation;
        }
    }
    return elevations;
}
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 8 && argc != 10) {
            throw std::runtime_error("Usage: render SHADERS TRACE WIDTH HEIGHT SKY_0 SKY_1 SKY_2 "
                                     "[FIRST_FRAME FRAME_COUNT]");
        }
        const int width = integer_argument(argv[3], 1, 3840);
        const int height = integer_argument(argv[4], 1, 2160);
        const int first = argc == 10 ? integer_argument(argv[8], 0, frames - 1) : 0;
        const int count = argc == 10 ? integer_argument(argv[9], 1, frames - first) : frames;
        const auto trace = read_trace(argv[2]);

        std::array<float, cells> bed{};
        std::copy_n(trace.begin(), cells, bed.begin());
        soften_cells(bed);
        const auto idle_stage = starting_surface(trace);
        SoftwareContext software;
        software.start(width, height);
        const GLuint program = make_program(argv[1], "relief");
        const GLuint sky_program = make_program(argv[1], "sky");
        GLuint sky_vao{};
        glGenVertexArrays(1, &sky_vao);
        upload_sky({argv[5], argv[6], argv[7]}, width, height);
        glUseProgram(sky_program);
        glUniform1i(glGetUniformLocation(sky_program, "sky"), 4);
        const GLint clip_to_sky_uniform = glGetUniformLocation(sky_program, "clip_to_sky");
        const GLint sky_phase_uniform = glGetUniformLocation(sky_program, "clock_progress");

        const Mesh grid = make_grid(reach_start, reach_end);
        const Mesh cut = make_cut_faces(reach_start, reach_end);
        make_texture(0, bed.data());
        const GLuint depth_texture = make_texture(1, nullptr);
        const auto floor = channel_floor(trace);
        make_texture(2, floor.data());
        make_texture(3, idle_stage.data());
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "bed"), 0);
        glUniform1i(glGetUniformLocation(program, "depth"), 1);
        glUniform1i(glGetUniformLocation(program, "channel_floor"), 2);
        glUniform1i(glGetUniformLocation(program, "idle_stage"), 3);
        const GLint surface_uniform = glGetUniformLocation(program, "surface");
        const GLint camera_uniform = glGetUniformLocation(program, "camera");
        const GLint eye_uniform = glGetUniformLocation(program, "eye");
        const GLint sun_uniform = glGetUniformLocation(program, "sun_direction");
        const GLint sun_tint_uniform = glGetUniformLocation(program, "sun_tint");
        const auto draw = [surface_uniform](Surface surface, const Mesh& mesh) {
            glUniform1i(surface_uniform, static_cast<GLint>(surface));
            mesh.draw();
        };

        const auto orbit_direction = [](float yaw, float pitch) {
            return glm::vec3(-std::sin(yaw) * std::cos(pitch), -std::cos(yaw) * std::cos(pitch),
                             std::sin(pitch));
        };
        const float aspect = static_cast<float>(width) / height;
        const glm::mat4 lens = glm::perspective(glm::radians(32.f), aspect, 0.5f, 700.f);
        // The sky plates share a fixed 65° camera with room for the relief's move.
        const glm::vec3 up{0.f, 0.f, 1.f};
        const glm::mat4 sky_camera =
            glm::perspective(glm::radians(65.f), aspect, 0.5f, 700.f) *
            glm::lookAt(glm::vec3{0.f}, -orbit_direction(glm::radians(12.f), glm::radians(53.f)),
                        up);
        glViewport(0, 0, width, height);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_MULTISAMPLE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4);
        std::array<float, cells> depth{};
        for (int frame = first; frame < first + count; ++frame) {
            const float progress = static_cast<float>(frame) / (frames - 1);
            const float ease = progress * progress * (3.f - 2.f * progress);
            const float yaw = glm::radians(std::lerp(3.f, 22.f, ease));
            const float descent = std::min(static_cast<float>(frame) / 275.f, 1.f);
            const float descent_ease = descent * descent * (3.f - 2.f * descent);
            const float pitch = glm::radians(std::lerp(57.f, 39.f, descent_ease));
            const glm::vec3 target{121.5f, std::lerp(47.5f, 43.5f, ease), 4.65f};
            const glm::vec3 eye =
                target + std::lerp(248.f, 251.f, ease) * orbit_direction(yaw, pitch);
            const glm::mat4 view = glm::lookAt(eye, target, up);
            const glm::mat4 camera = lens * view;
            const glm::mat4 clip_to_sky =
                sky_camera * glm::inverse(lens * glm::mat4(glm::mat3(view)));
            glClear(GL_DEPTH_BUFFER_BIT);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glUseProgram(sky_program);
            glUniformMatrix4fv(clip_to_sky_uniform, 1, GL_FALSE, glm::value_ptr(clip_to_sky));
            glUniform1f(sky_phase_uniform, progress);
            glBindVertexArray(sky_vao);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glEnable(GL_DEPTH_TEST);
            glUseProgram(program);
            glUniformMatrix4fv(camera_uniform, 1, GL_FALSE, glm::value_ptr(camera));
            glUniform3fv(eye_uniform, 1, glm::value_ptr(eye));
            // Use the same equinox clock as the baked Atmosphere sky.
            cubey::render::AtmosphereEnvironmentConfig atmosphere;
            atmosphere.time_of_day.time_hours = std::lerp(14.f, 17.f, progress);
            const auto sun =
                cubey::render::atmosphere_environment_solar_position(atmosphere.time_of_day);
            atmosphere.sun_elevation_degrees = sun.elevation_degrees;
            atmosphere.sun_azimuth_degrees = sun.azimuth_degrees;
            const auto lighting = cubey::render::atmosphere_environment_lighting(atmosphere);
            const glm::vec3 solar = lighting.sun_direction;
            const glm::vec3 sun_world{solar.x, -solar.z, solar.y};
            const glm::vec3 daylight{1.f, 0.94f, 0.82f};
            const glm::vec3 sun_tint =
                glm::mix(glm::vec3{1.f}, lighting.sun_color / daylight, 0.45f);
            glUniform3fv(sun_uniform, 1, glm::value_ptr(sun_world));
            glUniform3fv(sun_tint_uniform, 1, glm::value_ptr(sun_tint));

            const double sample = simulation_seconds_per_frame * frame / snapshot_seconds;
            const int before = static_cast<int>(sample);
            const float fraction = static_cast<float>(sample - before);
            const int depth_offset = (before * fields_per_snapshot + 1) * cells;
            for (int index = 0; index < cells; ++index) {
                depth[index] =
                    std::lerp(trace[depth_offset + index],
                              trace[depth_offset + fields_per_snapshot * cells + index], fraction);
            }
            soften_cells(depth);
            upload(1, depth_texture, depth.data());

            draw(Surface::CutBed, cut);
            draw(Surface::Bed, grid);
            glEnable(GL_BLEND);
            draw(Surface::Water, grid);
            draw(Surface::CutWater, cut);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            if (glGetError() != GL_NO_ERROR ||
                std::fwrite(pixels.data(), 1, pixels.size(), stdout) != pixels.size()) {
                throw std::runtime_error("Narrows and Sweep raster or frame write failed");
            }
        }
        if (std::fflush(stdout) != 0) {
            throw std::runtime_error("Could not flush the captured frames");
        }
        std::cerr << "Rendered " << count << " frame(s)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
