#include "Shader.h"

#include <sstream>
#include "ShaderProgram.h"

namespace cubey {
	Shader::Shader(GLenum type, const std::string& shader_key) : type_(type), shader_key_(shader_key), is_compiled_(false), gl_(0) {
		gl_ = glCreateShader(type_);
		if (!gl_) {
			std::cerr << "*** Poop: Failed to create shader object: " << shader_key_ << std::endl;
			assert(false);
		}
	}

	Shader::~Shader() {
		if (gl_) {
			glDeleteShader(gl_);
		}
	}

	void Shader::Compile(const char* source) {
		glShaderSource(gl_, 1, &source, NULL);
		glCompileShader(gl_);

		GLint compile_status;
		glGetShaderiv(gl_, GL_COMPILE_STATUS, &compile_status);
		if (!compile_status) {
			std::cerr << "*** Poop: Failed to compile shader: " << shader_key_ << std::endl;

			GLint info_log_length = 0;
			glGetShaderiv(gl_, GL_INFO_LOG_LENGTH, &info_log_length);
			if (info_log_length > 0) {
				char* info_log = new char[info_log_length];
				glGetShaderInfoLog(gl_, info_log_length, NULL, info_log);
				std::cerr << info_log << std::endl;
				delete[] info_log;
			}
			assert(false);
		}
		else {
			is_compiled_ = true;
		}
	}	

	void Shader::AttachTo(const ShaderProgram& shader_program) {
		if (!is_compiled_) {
			std::cerr << "*** Poop: Cannot attach invalid shader:" << shader_key_ << std::endl;
			assert(false);
		}
		glAttachShader(shader_program.gl_, gl_);
	}

	
}