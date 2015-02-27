#pragma once

#include <iostream>
#include <fstream>
#include <regex>

#include "Mesh.h"
#include "Util.h"

namespace cubey {
	class ObjLoader
	{
	public:
		static MeshIndexed* LoadObj(const std::string& file_name, bool reverse_winding = true, bool flip_normal = false) {

			std::ifstream f_stream;
			std::vector<std::string> possible_paths = { "./", "./res/", "../res/", };
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
				std::cerr << "*** Poop: Failed to open obj file: " << file_name << std::endl;
				assert(false);
			}

			std::vector<VPosition3> positions;
			std::vector<VNormal> normals;
			std::vector<unsigned int> indices;

			Vertex<VPosition3, VNormal>::Array verts;
			std::map<std::pair<int, int>, int> face_2_ind;

			for (std::string line; std::getline(f_stream, line);) {
				auto tokens = Tokenize(line, " ");
				if (tokens.size() <= 1) continue;
				if (tokens[0] == "v") {
					positions.push_back({ std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3]) });
				}
				else if (tokens[0] == "vn") {
					normals.push_back({ std::stof(tokens[1]), std::stof(tokens[2]), std::stof(tokens[3]) });
					if (flip_normal) normals[normals.size() - 1] = -normals[normals.size() - 1];
				}
				else if (tokens[0] == "f") {
					auto vert = Tokenize(tokens[1], "/");
					std::pair<int, int> inds;
					if (vert.size() == 1) {
						inds = std::make_pair(std::stoi(vert[0]) - 1, std::stoi(vert[0]) - 1);
					}
					else {
						inds = std::make_pair(std::stoi(vert[0]) - 1, std::stoi(vert[vert.size() - 1]) - 1);
					}
					auto it = face_2_ind.find(inds);
					if (it == face_2_ind.end()) {
						verts.push_back({ positions[inds.first], normals[inds.second] });
						int ind = verts.size() - 1;
						indices.push_back(ind);
						face_2_ind[inds] = ind;
					}
					else {
						indices.push_back(it->second);
					}

					vert = Tokenize(tokens[2], "/");
					if (vert.size() == 1) {
						inds = std::make_pair(std::stoi(vert[0]) - 1, std::stoi(vert[0]) - 1);
					}
					else {
						inds = std::make_pair(std::stoi(vert[0]) - 1, std::stoi(vert[vert.size() - 1]) - 1);
					}
					it = face_2_ind.find(inds);
					if (it == face_2_ind.end()) {
						verts.push_back({ positions[inds.first], normals[inds.second] });
						int ind = verts.size() - 1;
						indices.push_back(ind);
						face_2_ind[inds] = ind;
					}
					else {
						indices.push_back(it->second);
					}

					vert = Tokenize(tokens[3], "/");
					if (vert.size() == 1) {
						inds = std::make_pair(std::stoi(vert[0]) - 1, std::stoi(vert[0]) - 1);
					}
					else {
						inds = std::make_pair(std::stoi(vert[0]) - 1, std::stoi(vert[vert.size() - 1]) - 1);
					}
					it = face_2_ind.find(inds);
					if (it == face_2_ind.end()) {
						verts.push_back({ positions[inds.first], normals[inds.second] });
						int ind = verts.size() - 1;
						indices.push_back(ind);
						face_2_ind[inds] = ind;
					}
					else {
						indices.push_back(it->second);
					}

					if (reverse_winding) {
						int temp = indices[indices.size() - 1];
						indices[indices.size() - 1] = indices[indices.size() - 2];
						indices[indices.size() - 2] = temp;
					}
					
				}

			}

			return MeshIndexed::Create(verts, indices, GL_TRIANGLES);
		}
	};
}


