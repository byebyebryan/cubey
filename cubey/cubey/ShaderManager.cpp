#include "ShaderManager.h"

#include <iostream>
#include <fstream>
#include <regex>

namespace cubey {

	ShaderProgram* ShaderManager::CreateProgram(const std::string& program_key) {
		std::vector<std::string> tokens = Tokenize(program_key);
		if (tokens.size() <= 1) {
			std::cerr << "*** Poop: Invalid Program Key: " << program_key << std::endl;
			assert(false);
		}

		ShaderProgram* program = new ShaderProgram(program_key);
		LoadFile(tokens[0]);

		for (int i = 1; i < tokens.size(); i++) {
			std::string token;
			if (std::regex_match(tokens[i], std::regex("__.*__"))) {
				token = tokens[i];
			}
			else {
				token.append("__");
				token.append(tokens[i]);
				token.append("__");
			}
			
			if (directives_.find(token) == directives_.end()) {
				std::cerr << "*** Poop: Invalid Shader Key: " << token << std::endl;
				assert(false);
			}
			else {
				GLenum shader_type = GetShaderType(token);

				if (shader_type == GL_INVALID_ENUM) {
					std::cerr << "*** Poop: Invalid Shader Key: " << token << std::endl;
					assert(false);
				}

				std::stringstream shader_source;
				shader_source << "#version 430" << std::endl << "#define " << token << std::endl;
				shader_source << buffer_.str();
				std::string source_str = shader_source.str();

				program->AddShader(shader_type, token, source_str.c_str());
			}
		}

		program->Link();

		programs_[program_key] = program;
		return program;
	}

	ShaderProgram* ShaderManager::GetProgram(const std::string& program_key) {
		auto it = programs_.find(program_key);
		if (it != programs_.end()) {
			return it->second;
		}
		return nullptr;
	}

	void ShaderManager::DestroyProgram(const std::string& program_key) {
		auto it = programs_.find(program_key);
		if (it != programs_.end()) {
			if (it->second) {
				delete it->second;
			}
			programs_.erase(it);
		}
	}

	void ShaderManager::ClearBuffer() {
		buffer_.clear();
		buffer_.str(std::string());
		directives_.clear();
	}

	std::vector<std::string> ShaderManager::Tokenize(const std::string& str, const std::string& delimiters /*= "."*/) {
		std::vector<std::string> tokens;
		std::string::size_type last_pos = str.find_first_not_of(delimiters);
		std::string::size_type pos = str.find_first_of(delimiters, last_pos);

		while (pos != std::string::npos || last_pos != std::string::npos) {
			tokens.push_back(str.substr(last_pos, pos - last_pos));
			last_pos = str.find_first_not_of(delimiters, pos);
			pos = str.find_first_of(delimiters, last_pos);
		}
		return tokens;
	}

	void ShaderManager::LoadFile(const std::string& file_name) {
		if (!current_file_.compare(file_name)) {
			return;
		}

		std::ifstream f_stream;
		std::vector<std::string> possible_paths = { "shaders/", "../shaders/", "./" };
		for (auto& path : possible_paths) {
			std::string full_name;
			full_name.append(path);
			full_name.append(file_name);
			full_name.append(".glsl");

			f_stream.open(full_name);
			if (f_stream.is_open()) {
				break;
			}
		}

		if (!f_stream.is_open()) {
			std::cerr << "*** Poop: Failed to open shader file: " << file_name << std::endl;
			assert(false);
		}

		ClearBuffer();
		
		for (std::string line; std::getline(f_stream, line);) {
			if (std::regex_match(line, std::regex("__.+__"))) {
				directives_.insert(line);
				if (directives_.size() > 1) {
					buffer_ << "#endif" << std::endl;
				}
				buffer_ << "#ifdef " << line << std::endl;
			}
			else {
				buffer_ << line << std::endl;
			}
		}

		buffer_ << "#endif" << std::endl;
	}

	GLenum ShaderManager::GetShaderType(const std::string& shader_key) {
		if (std::regex_search(shader_key, std::regex("VS|VERTEX"))) {
			return GL_VERTEX_SHADER;
		}
		if (std::regex_search(shader_key, std::regex("GS|GEOMETRY"))) {
			return GL_GEOMETRY_SHADER;
		}
		if (std::regex_search(shader_key, std::regex("FS|FRAGMENT"))) {
			return GL_FRAGMENT_SHADER;
		}
		if (std::regex_search(shader_key, std::regex("CS|COMPUTE"))) {
			return GL_COMPUTE_SHADER;
		}
		return GL_INVALID_ENUM;
	}

	

	

	

	

}