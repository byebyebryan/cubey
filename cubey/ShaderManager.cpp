#include "ShaderManager.h"

#include <iostream>
#include <fstream>
#include <regex>

namespace cubey {

	std::set<std::string> ShaderManager::BufferCleaner::files_ = std::set<std::string>();

	void ShaderManager::BufferCleaner::Clear() {
		for (auto& file_name : files_) {
			ShaderManager::Get()->UnloadFile(file_name);
		}
		files_.clear();
	}

	ShaderProgram* ShaderManager::CreateProgram(const std::string& program_key) {
		std::vector<std::string> tokens = Tokenize(program_key);
		if (tokens.size() <= 1) {
			std::cerr << "*** Poop: Invalid Program Key: " << program_key << std::endl;
			assert(false);
		}

		ShaderProgram* program = new ShaderProgram(program_key);
		std::string file_name = tokens[0];

		LoadFile(file_name);


		ShaderFile& shader_file = files_[file_name];

		for (int i = 1; i < tokens.size(); i++) {
			std::string token;
			if (std::regex_match(tokens[i], std::regex("__(VS|VERTEX|GS|GEOMETRY|FS|FRAGMENT|CS|COMPUTE).*__"))) {
				token = tokens[i];
			}
			else if (std::regex_match(tokens[i], std::regex("(VS|VERTEX|GS|GEOMETRY|FS|FRAGMENT|CS|COMPUTE).*"))) {
				token.append("__");
				token.append(tokens[i]);
				token.append("__");
			}
			else {
				std::cerr << "*** Poop: Invalid Shader Key: " << file_name << "." << token << std::endl;
				assert(false);
			}
			
			if (shader_file.blocks.find(token) == shader_file.blocks.end()) {
				std::cerr << "*** Poop: Invalid Shader Key: " << file_name << "." << token << std::endl;
				assert(false);
			}
			else {
				GLenum shader_type = GetShaderType(token);

				if (shader_type == GL_INVALID_ENUM) {
					std::cerr << "*** Poop: Invalid Shader Key: " << file_name << "." << token << std::endl;
					assert(false);
				}

				std::string shader_source;
				shader_source.append("#version 430\n");
				shader_source.append(shader_file.blocks["Common"]);
				shader_source.append(shader_file.blocks[token]);

				program->AddShader(shader_type, token, shader_source.c_str());
			}
		}

		BufferCleaner::files_.insert(file_name);

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

	
	void ShaderManager::LoadFile(const std::string& file_name) {

		auto it = files_.find(file_name);
		if (it != files_.end()) {
			return;
		}

		ShaderFile new_file;

		std::ifstream f_stream;
		std::vector<std::string> possible_paths = { "./", "./shaders/", "../shaders/",  };
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
		
		int line_num = 0;
		new_file.blocks["Common"] = " ";
		auto current_block = new_file.blocks.begin();
		for (std::string line; std::getline(f_stream, line); line_num++) {
			if (std::regex_match(line, std::regex("__(VS|VERTEX|GS|GEOMETRY|FS|FRAGMENT|CS|COMPUTE).*__"))) {
				auto it = new_file.blocks.find(line);
				if (it == new_file.blocks.end()) {
					new_file.blocks[line] = "#line "+ std::to_string(line_num + 2) + "\n";
					current_block = new_file.blocks.find(line);
				}
				else {
					std::cerr << "*** Poop: Replicated Shader Key: " << file_name << "." << line << std::endl;
					assert(false);
				}
			}
			else if(std::regex_match(line, std::regex("#include.+"))) {
				std::string include_file_name = std::regex_replace(line, std::regex("#include"), "");
				include_file_name = std::regex_replace(include_file_name, std::regex("\\s"), "");
				include_file_name = std::regex_replace(include_file_name, std::regex("[<>\"]"), "");

				current_block->second.append(GetInclude(include_file_name) + "\n");
				current_block->second.append("#line " + std::to_string(line_num + 2) + "\n");
			}
			else {
				current_block->second.append(line + "\n");
			}
		}

		files_[file_name] = new_file;
	}

	std::string ShaderManager::GetInclude(const std::string& file_name) {
		std::ifstream f_stream;
		std::vector<std::string> possible_paths = { "./", "./shaders/", "../shaders/", };
		for (auto& path : possible_paths) {
			std::string full_name;
			full_name.append(path);
			full_name.append(file_name);

			f_stream.open(full_name);
			if (f_stream.is_open()) {
				break;
			}
		}

		if (!f_stream.is_open()) {
			std::cerr << "*** Poop: Failed to open include file: " << file_name << std::endl;
			assert(false);
		}

		std::stringstream s_stream;
		s_stream << f_stream.rdbuf();
		return s_stream.str();
	}

	void ShaderManager::UnloadFile(const std::string& file_name) {
		auto it = files_.find(file_name);
		if (it == files_.end()) {
			return;
		}

		files_.erase(it);
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