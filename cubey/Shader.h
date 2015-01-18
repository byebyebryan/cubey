#pragma once

#include "GL/glew.h"
#include <iostream>
#include <vector>
#include <unordered_map>
#include "glm/glm.hpp"
#include "glm/gtc/type_ptr.hpp"

namespace cubey {
	class Shader {
	public:
		Shader(GLenum, const std::string&);
		~Shader();

		bool Compile(const std::string& preprocessor_def);
		bool AttachTo(const class ShaderProgram& shader_program);

		const GLenum type_;
		const std::string file_name_;
	private:
		GLuint gl_;
		bool is_compiled_;
	};

	class ShaderProgram {
	public:
		ShaderProgram();
		~ShaderProgram();

		bool AddShader(GLenum shader_type, const std::string& file_name, const std::string& preprocessor_def);
		bool Link();
		bool Activate();

		template<typename T>
		void SetUniform(int index, const T& value) {
			SetUniformImpl(uniform_var_index_to_location[index], value);
		}
		template<typename T>
		void SetUniform(const std::string& name, const T& value) {
			SetUniformImpl(uniform_var_name_to_location[name], value);
		}

		GLuint gl_;
	private:
		void QueryUniforms();

		void SetUniformImpl(GLint location, const int& value) {
			glUniform1i(location, value);
		}
		void SetUniformImpl(GLint location, const unsigned int& value) {
			glUniform1ui(location, value);
		}
		void SetUniformImpl(GLint location, const float& value) {
			glUniform1f(location, value);
		}
		void SetUniformImpl(GLint location, const double& value) {
			glUniform1d(location, value);
		}
		void SetUniformImpl(GLint location, const glm::vec2& value) {
			glUniform2fv(location, 1, glm::value_ptr(value));
		}
		void SetUniformImpl(GLint location, const glm::vec3& value) {
			glUniform3fv(location, 1, glm::value_ptr(value));
		}
		void SetUniformImpl(GLint location, const glm::vec4& value) {
			glUniform4fv(location, 1, glm::value_ptr(value));
		}
		void SetUniformImpl(GLint location, const glm::mat2& value) {
			glUniformMatrix2fv(location, 1, GL_FALSE, glm::value_ptr(value));
		}
		void SetUniformImpl(GLint location, const glm::mat3& value) {
			glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(value));
		}
		void SetUniformImpl(GLint location, const glm::mat4& value) {
			glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
		}

		std::vector<Shader> shaders_;
		std::vector<GLint> uniform_var_index_to_location;
		std::unordered_map<std::string, GLint> uniform_var_name_to_location;
		bool is_linked_;
	};
}


