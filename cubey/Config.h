#pragma once

#include "Interfaces.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include "tinyxml2.h"

namespace cubey {
	using namespace tinyxml2;

	class Config : public ISingleton < Config > {

		class Entry {
			enum Type {
				BOOL, INT, FLOAT, VEC3, COLOR4F
			};

			void* data_ptr_;
			Type data_type_;
		};

	public:
		void ReadFromFile(const std::string & file_name) {}
		void ReadFromBuffer(const std::string & buffer) {}

		void WriteToFile(const std::string & file_name) {}
		void WriteToBuffer(const std::string & buffer) {}

	};
}


