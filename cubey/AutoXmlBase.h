#pragma once

#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <functional>
#include <assert.h>
#include "AutoXmlTypes.h"
#include "TwUI.h"

#ifndef AUTO_XML_VAR
#define AUTO_XML_VAR(var_type, var_name, var_default) \
	private: class AutoXml_##var_name { \
	public: \
		std::string name_; \
		var_type value_; \
		AutoXml_##var_name () : name_(#var_name) { \
			value_ = var_default; \
			AutoXmlClass::reading_funcs()[#var_name] = std::bind(&AutoXmlType<var_type>::Read, std::placeholders::_1, &value_); \
			AutoXmlClass::writing_funcs().push_back(std::bind(&AutoXmlType<var_type>::Write, std::placeholders::_1, name_.c_str(), &value_)); \
		} \
	}; \
	private: AutoXml_##var_name auto_xml_##var_name##_; \
	public: var_type var_name() {return auto_xml_##var_name##_.value_;} \
	public: var_type * var_name##_ptr() {return &auto_xml_##var_name##_.value_;} \
	public: void set_##var_name(const var_type & value) {auto_xml_##var_name##_.value_ = value;}

#endif

#ifndef AUTO_XML_VAR_TW
#define AUTO_XML_VAR_TW(var_type, var_name, var_default, var_tw_define) \
	private: class AutoXml_##var_name { \
	public: \
		std::string name_; \
		var_type value_; \
		AutoXml_##var_name () : name_(#var_name) { \
			value_ = var_default; \
			AutoXmlClass::reading_funcs()[#var_name] = std::bind(&AutoXmlType<var_type>::Read, std::placeholders::_1, &value_); \
			AutoXmlClass::writing_funcs().push_back(std::bind(&AutoXmlType<var_type>::Write, std::placeholders::_1, name_.c_str(), &value_)); \
			AutoXmlClass::tw_adding_funcs().push_back([this](){TwUI::Get()->AddRW(name_.c_str(), AutoXmlType<var_type>::tw_type(), &value_, var_tw_define); }); \
			AutoXmlClass::tw_removing_funcs().push_back([this](){TwUI::Get()->RemoveEntry(name_.c_str()); }); \
		} \
	}; \
	private: AutoXml_##var_name auto_xml_##var_name##_; \
	public: var_type var_name() {return auto_xml_##var_name##_.value_;} \
	public: var_type * var_name##_ptr() {return &auto_xml_##var_name##_.value_;} \
	public: void set_##var_name(const var_type & value) {auto_xml_##var_name##_.value_ = value;}

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
			while (xmlNode) {
				auto it = reading_funcs().find(xmlNode->Name());
				if (it != reading_funcs().end()) {
					it->second(xmlNode);
				}
				else {
					std::cerr << "*** Poop: Invalid XML Entry: " << xmlNode->Name() << std::endl;
					assert(false);
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

			for (auto & it : writing_funcs()) {
				it(rootElement);
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

		void TwBarInit() {
			for (auto & it : tw_adding_funcs()) {
				it();
			}
		}

		void TwBarRemove() {
			for (auto & it : tw_removing_funcs()) {
				it();
			}
		}

	protected:
		//reading/writing function maps
		static std::unordered_map<std::string, std::function<void(XMLElement *)>> & reading_funcs() {
			static std::unordered_map<std::string, std::function<void(XMLElement *)>> funcs;
			return funcs;
		};
		static std::vector<std::function<void(XMLElement *)>> & writing_funcs() {
			static std::vector<std::function<void(XMLElement *)>> funcs;
			return funcs;
		}

		static std::vector<std::function<void()>> & tw_adding_funcs() {
			static std::vector<std::function<void()>> funcs;
			return funcs;
		}
		static std::vector<std::function<void()>> & tw_removing_funcs() {
			static std::vector<std::function<void()>> funcs;
			return funcs;
		}
	};
}


