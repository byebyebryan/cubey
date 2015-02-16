#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <functional>
#include <assert.h>
#include "tinyxml2.h"
#include "AutoXmlTypes.h"

#ifndef AUTO_XML_VAR
#define AUTO_XML_VAR(var_type, var_name) \
	private: template <typename T0> class AutoXML_##var_name { \
	public: \
		std::string name_; \
		var_type value_; \
		AutoXML_##var_name () : name_(#var_name) { \
			value_ = var_type(); \
			T0::reading_funcs()[#var_name] = std::bind(&AutoXmlType<var_type>::Read, std::placeholders::_1, &value_); \
			T0::writing_funcs()[#var_name] = std::bind(&AutoXmlType<var_type>::Write, std::placeholders::_1, #var_name, std::cref(value_)); \
								} \
				}; \
	private: AutoXML_##var_name<AutoXmlClass> var_name##_; \
	public: var_type var_name() {return var_name##_.value_;} \
	public: var_type * var_name_ptr() {return &var_name##_.value_;} \
	public: void set_##var_name(const var_type & value) {var_name##_.value_ = value;}

#endif

namespace cubey {
	using namespace tinyxml2;

	template<typename T>
	class AutoXmlBase {
	public:
		typedef T AutoXmlClass;

		//stream in from xml
		void ReadFromFile(const std::string & file_name) {
			std::ifstream fileStream(file_name);
			std::stringstream stringBuffer;
			stringBuffer << fileStream.rdbuf();
			ReadFromBuffer(stringBuffer.str());
		}
		void ReadFromBuffer(const std::string & buffer) {
			XMLDocument * xmlDoc = new XMLDocument();
			xmlDoc->Parse(buffer.c_str());
			XMLElement * xmlNode = xmlDoc->RootElement()->FirstChildElement();
			while (xmlNode)
			{
				auto it = reading_funcs().find(xmlNode->Name());
				if (it != reading_funcs().end())
				{
					it->second(xmlNode);
				}
				xmlNode = xmlNode->NextSiblingElement();
			}
			delete xmlDoc;
		}

		//stream out to xml
		void WriteToBuffer(std::string & buffer) {
			XMLDocument * xmlDoc = new XMLDocument();
			XMLDeclaration * xmlDecl = xmlDoc->NewDeclaration();
			xmlDoc->InsertFirstChild(xmlDecl);
			XMLElement * rootElement = xmlDoc->NewElement("root");
			xmlDoc->InsertEndChild(rootElement);

			for (auto & it : writing_funcs())
			{
				it.second(rootElement);
			}

			XMLPrinter xmlPrinter;
			xmlDoc->Print(&xmlPrinter);
			buffer = std::string(xmlPrinter.CStr());
		}
		void WriteToFile(const std::string & file_name) {
			std::string buffer;
			WriteToBuffer(buffer);
			std::ofstream fileStream(file_name);
			fileStream << buffer;
		}

	protected:
		//reading/writing function maps
		static std::unordered_map<std::string, std::function<void(tinyxml2::XMLElement *)>> & reading_funcs() {
			static std::unordered_map<std::string, std::function<void(tinyxml2::XMLElement *)>> funcs;
			return funcs;
		};
		static std::unordered_map<std::string, std::function<void(tinyxml2::XMLElement *)>> & writing_funcs() {
			static std::unordered_map<std::string, std::function<void(tinyxml2::XMLElement *)>> funcs;
			return funcs;
		}
	};
}


