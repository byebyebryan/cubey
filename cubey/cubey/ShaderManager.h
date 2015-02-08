#pragma once

#include <vector>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <set>
#include "GL/glew.h"

#include "ShaderProgram.h"
#include "Interfaces.h"

namespace cubey {

	class ShaderManager : public ISingleton<ShaderManager> {
	public:
		ShaderProgram* CreateProgram(const std::string& program_key);
		ShaderProgram* GetProgram(const std::string& program_key);
		void DestroyProgram(const std::string& program_key);
		void ClearBuffer();

	private:
		void LoadFile(const std::string& file_name);
		std::vector<std::string> Tokenize(const std::string& str, const std::string& delimiters = ".");
		GLenum GetShaderType(const std::string& shader_key);

		std::string current_file_;
		std::stringstream buffer_;
		std::set<std::string> directives_;
		std::unordered_map<std::string, ShaderProgram*> programs_;
	};
}