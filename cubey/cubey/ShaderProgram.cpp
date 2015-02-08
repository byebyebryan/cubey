#include "ShaderProgram.h"

#include <iostream>
#include <fstream>
#include <sstream>


namespace cubey {
	ShaderProgram::ShaderProgram(const std::string& program_key) : program_key_(program_key), is_linked_(false), gl_(0) {
		gl_ = glCreateProgram();
		if (!gl_) {
			std::cerr << "*** Poop: Failed to create shader program object: " << program_key_ << std::endl;
			assert(false);
		}
	}

	ShaderProgram::~ShaderProgram() {
		if (gl_) {
			glDeleteProgram(gl_);
		}
	}

	void ShaderProgram::AddShader(GLenum shader_type, const std::string& shader_key, const char* source) {
		shaders_.emplace_back(shader_type, shader_key);
		shaders_.back().Compile(source);
		shaders_.back().AttachTo(*this);
	}

	void ShaderProgram::Link() {
		glLinkProgram(gl_);
		GLint link_status;
		glGetProgramiv(gl_, GL_LINK_STATUS, &link_status);
		if (!link_status) {
			std::cerr << "*** Poop: Failed to link shader program: " << program_key_ << std::endl;

			GLint info_log_length = 0;
			glGetProgramiv(gl_, GL_INFO_LOG_LENGTH, &info_log_length);
			if (info_log_length > 0) {
				char* info_log = new char[info_log_length];
				glGetProgramInfoLog(gl_, info_log_length, NULL, info_log);
				std::cerr << info_log << std::endl;
				delete[] info_log;
			}
			assert(false);
		}
		else {
			is_linked_ = true;
			std::cout << "Linked program: " << program_key_ << std::endl;
			QueryUniforms();
		}
	}

	void ShaderProgram::Activate() {
		if (!is_linked_) {
			std::cerr << "*** Poop: Cannot use invalid program: " << program_key_ << std::endl;
			assert(false);
		}
		GLint current_prog;
		glGetIntegerv(GL_CURRENT_PROGRAM, &current_prog);
		if (current_prog != gl_) {
			glUseProgram(gl_);
		}
	}

	void ShaderProgram::QueryUniforms() {
		GLint num_active_uniforms;
		glGetProgramiv(gl_, GL_ACTIVE_UNIFORMS, &num_active_uniforms);

		GLint max_buffer_size;
		glGetProgramiv(gl_, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_buffer_size);

		std::cout << "Uniforms: " << std::endl;

		for (int i = 0; i < num_active_uniforms; i++) {
			GLint uniform_size;
			GLenum uniform_type;
			char* uniform_name = new char[max_buffer_size];
			glGetActiveUniform(gl_, i, max_buffer_size, NULL, &uniform_size, &uniform_type, uniform_name);
			GLint uniform_location = glGetUniformLocation(gl_, uniform_name);
			if (uniform_location >= 0) {
				uniform_var_name_to_location[std::string(uniform_name)] = uniform_location;
				std::cout << "location " << uniform_location << " : " << uniform_name << std::endl;
			}
			delete[] uniform_name;
		}

		std::cout << std::endl;
	}
}