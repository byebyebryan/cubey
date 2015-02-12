#pragma once

#include <vector>
#include <unordered_map>
#include "GL/glew.h"
#include "glm/glm.hpp"
#include "glm/gtc/type_ptr.hpp"

#include "Shader.h"

namespace cubey {

	class ShaderProgram {
	public:
		ShaderProgram(const std::string&);
		~ShaderProgram();

		void AddShader(GLenum shader_type, const std::string& shader_key, const char* source);
		void Link();
		void Activate();

		inline int GetUniformLocation(const std::string& name) {
			auto it = uniform_var_name_to_location.find(name);
			if (it == uniform_var_name_to_location.end()) {
				return -1;
			}
			else {
				return it->second;
			}
		}

		template<typename T>
		void SetUniform(int location, const T& value) {
			SetUniformImpl(location, value);
		}
		template<typename T>
		void SetUniform(const std::string& name, const T& value) {
			SetUniformImpl(uniform_var_name_to_location[name], value);
		}

		GLuint gl_;
		const std::string program_key_;
	private:
		void QueryUniforms();

		inline void SetUniformImpl(GLint location, const int& value) {
			glProgramUniform1i(gl_, location, value);
		}
		inline void SetUniformImpl(GLint location, const unsigned int& value) {
			glProgramUniform1ui(gl_, location, value);
		}
		inline void SetUniformImpl(GLint location, const float& value) {
			glProgramUniform1f(gl_, location, value);
		}
		inline void SetUniformImpl(GLint location, const double& value) {
			glProgramUniform1d(gl_, location, value);
		}
		inline void SetUniformImpl(GLint location, const glm::vec2& value) {
			glProgramUniform2fv(gl_, location, 1, glm::value_ptr(value));
		}
		inline void SetUniformImpl(GLint location, const glm::vec3& value) {
			glProgramUniform3fv(gl_, location, 1, glm::value_ptr(value));
		}
		inline void SetUniformImpl(GLint location, const glm::vec4& value) {
			glProgramUniform4fv(gl_, location, 1, glm::value_ptr(value));
		}
		inline void SetUniformImpl(GLint location, const std::vector<glm::vec2>& value) {
			glProgramUniform2fv(gl_, location, value.size(), (const GLfloat*)value.data());
		}
		inline void SetUniformImpl(GLint location, const std::vector<glm::vec3>& value) {
			glProgramUniform3fv(gl_, location, value.size(), (const GLfloat*)value.data());
		}
		inline void SetUniformImpl(GLint location, const std::vector<glm::vec4>& value) {
			glProgramUniform4fv(gl_, location, value.size(), (const GLfloat*)value.data());
		}
		inline void SetUniformImpl(GLint location, const glm::mat2& value) {
			glProgramUniformMatrix2fv(gl_, location, 1, GL_FALSE, glm::value_ptr(value));
		}
		inline void SetUniformImpl(GLint location, const glm::mat3& value) {
			glProgramUniformMatrix3fv(gl_, location, 1, GL_FALSE, glm::value_ptr(value));
		}
		inline void SetUniformImpl(GLint location, const glm::mat4& value) {
			glProgramUniformMatrix4fv(gl_, location, 1, GL_FALSE, glm::value_ptr(value));
		}

		std::vector<Shader> shaders_;
		std::unordered_map<std::string, GLint> uniform_var_name_to_location;
		bool is_linked_;
	};
}