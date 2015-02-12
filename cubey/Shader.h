#pragma once

#include "GL/glew.h"
#include <iostream>
#include <cassert>

namespace cubey {
	class Shader {
	public:
		Shader(GLenum, const std::string&);
		~Shader();

		void Compile(const char* source);
		void AttachTo(const class ShaderProgram& shader_program);

		const GLenum type_;
		const std::string shader_key_;
	private:
		GLuint gl_;
		bool is_compiled_;
	};

	
}


