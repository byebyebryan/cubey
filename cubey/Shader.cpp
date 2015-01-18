#include "Shader.h"
#include <fstream>
#include <sstream>

namespace cubey {
	Shader::Shader(GLenum type, const std::string& file_name) : type_(type), file_name_(file_name), is_compiled_(false) {
		gl_ = glCreateShader(type_);
		if (!gl_) {
			std::cerr << "*** Poop: Failed to create shader object: " << file_name_ << std::endl;
		}
	}

	Shader::~Shader() {
		if (gl_) {
			glDeleteShader(gl_);
		}
	}

	bool Shader::Compile(const std::string& preprocessor_def) {
		if (!gl_) {
			std::cerr << "*** Poop: Invalid shader object" << file_name_ << std::endl;
			return false;
		}

		std::ifstream f_stream(file_name_);
		if (!f_stream.is_open()) {
			std::cerr << "*** Poop: Failed to open shader file: " << file_name_ << std::endl;
			return false;
		}

		std::stringstream s_stream;
		std::string line;
		std::getline(f_stream, line);
		s_stream << line << std::endl;
		s_stream << preprocessor_def << std::endl << f_stream.rdbuf();
		std::string str_buffer = s_stream.str();
		const char* buffer = str_buffer.c_str();
		glShaderSource(gl_, 1, &buffer, NULL);
		glCompileShader(gl_);

		GLint compile_status;
		glGetShaderiv(gl_, GL_COMPILE_STATUS, &compile_status);
		if (!compile_status) {
			std::cerr << "*** Poop: Failed to compile shader: " << file_name_ << std::endl;

			GLint info_log_length = 0;
			glGetShaderiv(gl_, GL_INFO_LOG_LENGTH, &info_log_length);
			if (info_log_length > 0) {
				char* info_log = new char[info_log_length];
				glGetShaderInfoLog(gl_, info_log_length, NULL, info_log);
				std::cerr << info_log << std::endl;
				delete[] info_log;
			}
			return false;
		}
		is_compiled_ = true;
		return true;
	}	

	bool Shader::AttachTo(const ShaderProgram& shader_program) {
		if (!is_compiled_) {
			std::cerr << "*** Poop: Cannot attach invalid shader:" << file_name_ << std::endl;
			return false;
		}
		glAttachShader(shader_program.gl_, gl_);
		return true;
	}

	ShaderProgram::ShaderProgram() {
		is_linked_ = false;
		gl_ = 0;
		gl_ = glCreateProgram();
		if (!gl_) {
			std::cerr << "*** Poop: Failed to create shader program object" << std::endl;
		}
	}

	ShaderProgram::~ShaderProgram() {
		if (gl_) {
			glDeleteProgram(gl_);
		}
	}

	bool ShaderProgram::AddShader(GLenum shader_type, const std::string& file_name, const std::string& preprocessor_def) {
		shaders_.emplace_back(shader_type, file_name);
		if (!shaders_.back().Compile(preprocessor_def)) {
			return false;
		}
		return shaders_.back().AttachTo(*this);
	}

	bool ShaderProgram::Link() {
		glLinkProgram(gl_);
		GLint link_status;
		glGetProgramiv(gl_, GL_LINK_STATUS, &link_status);
		if (!link_status) {
			std::cerr << "*** Poop: Failed to link shader program" << std::endl;

			GLint info_log_length = 0;
			glGetProgramiv(gl_, GL_INFO_LOG_LENGTH, &info_log_length);
			if (info_log_length > 0) {
				char* info_log = new char[info_log_length];
				glGetProgramInfoLog(gl_, info_log_length, NULL, info_log);
				std::cerr << info_log << std::endl;
				delete[] info_log;
			}
			return false;
		}
		is_linked_ = true;
		QueryUniforms();
		return true;
	}

	bool ShaderProgram::Activate() {
		if (!is_linked_) {
			std::cerr << "*** Poop: Cannot use invalid program" << std::endl;
			return false;
		}
		glUseProgram(gl_);
		return true;
	}

	void ShaderProgram::QueryUniforms() {
		GLint num_active_uniforms;
		glGetProgramiv(gl_, GL_ACTIVE_UNIFORMS, &num_active_uniforms);

		GLint max_buffer_size;
		glGetProgramiv(gl_, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_buffer_size);

		for (int i = 0; i < num_active_uniforms; i++) {
			GLint uniform_size;
			GLenum uniform_type;
			char* uniform_name = new char[max_buffer_size];
			glGetActiveUniform(gl_, i, max_buffer_size, NULL, &uniform_size, &uniform_type, uniform_name);
			GLint uniform_location = glGetUniformLocation(gl_, uniform_name);
			if (uniform_location >= 0) {
				uniform_var_index_to_location.push_back(uniform_location);
				uniform_var_name_to_location[std::string(uniform_name)] = uniform_location;
				std::cout << "Uniform Variable " << uniform_var_index_to_location.size() - 1 << " : " << uniform_name \
					<< " at location " << uniform_location << std::endl;
			}
			delete[] uniform_name;
		}
	}

	


	

}