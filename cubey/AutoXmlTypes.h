#pragma once

#include <iostream>
#include <vector>
#include <functional>
#include <assert.h>
#include "tinyxml2.h"
#include "AntTweakBar.h"
#include "glm/glm.hpp"

namespace cubey {
	using namespace tinyxml2;

	struct AColor3F {
		glm::vec3 vec;
	};

	struct AColor4F {
		glm::vec4 vec;
	};

	template<typename T>
	struct AutoXmlType
	{
		static void Read(XMLElement * xml_element, T * value)
		{
			std::cerr << "missing XML reading function for value type : " << typeid(T).name() << std::endl;
			assert(false);
		}

		static void Write(XMLElement * xml_element, const char * name, const T * value)
		{
			std::cerr << "missing XML writing function for value type : " << typeid(T).name() << std::endl;
			assert(false);
		}
	};

	// partially specialized read/write for container type std::vector<T>
	//  -calls fully specialized read/write for type T
	template<typename T>
	struct AutoXmlType < std::vector<T> >
	{
		static void Read(XMLElement * xml_element, std::vector<T> * value)
		{
			XMLElement * child_element = xml_element->FirstChildElement();
			while (child_element)
			{
				T child_value;
				VarType<T>::Read(child_element, &child_value);
				value->push_back(child_value);
				child_element = child_element->NextSiblingElement();
			}
		}

		static void Write(XMLElement * xml_element, const char * name, const std::vector<T> * value)
		{
			XMLElement * new_element = xml_element->GetDocument()->NewElement(name);
			new_element->SetAttribute("type", "std::vector");
			int i = 0;
			for (auto * item : *value)
			{
				VarType<T>::Write(new_element, "item_" + std::to_string(i), item);
				i++;
			}
			xml_element->InsertEndChild(new_element);
		}
	};

	// fully specialized read/write for bool
	template<>
	struct AutoXmlType < bool >
	{
		static TwType tw_type() { return TW_TYPE_BOOLCPP; }

		static void Read(XMLElement * xml_element, bool * value)
		{
			xml_element->QueryBoolAttribute("value", value);
		}

		static void Write(XMLElement * xml_element, const char * name, const bool * value)
		{
			XMLElement * new_element = xml_element->GetDocument()->NewElement(name);
			new_element->SetAttribute("type", "bool");
			new_element->SetAttribute("value", *value ? "true" : "false");
			xml_element->InsertEndChild(new_element);
		}
	};

	// fully specialized read/write for int
	template<>
	struct AutoXmlType < int >
	{
		static TwType tw_type() { return TW_TYPE_INT32; }

		static void Read(XMLElement * xml_element, int * value)
		{
			xml_element->QueryIntAttribute("value", value);
		}

		static void Write(XMLElement * xml_element, const char * name, const int * value)
		{
			XMLElement * new_element = xml_element->GetDocument()->NewElement(name);
			new_element->SetAttribute("type", "int");
			new_element->SetAttribute("value", *value);
			xml_element->InsertEndChild(new_element);
		}
	};

	// fully specialized read/write for float
	template<>
	struct AutoXmlType < float >
	{
		static TwType tw_type() { return TW_TYPE_FLOAT; }

		static void Read(XMLElement * xml_element, float * value)
		{
			xml_element->QueryFloatAttribute("value", value);
		}

		static void Write(XMLElement * xml_element, const char * name, const float * value)
		{
			XMLElement * new_element = xml_element->GetDocument()->NewElement(name);
			new_element->SetAttribute("type", "float");
			new_element->SetAttribute("value", *value);
			xml_element->InsertEndChild(new_element);
		}
	};

	// fully specialized read/write for double
	template<>
	struct AutoXmlType < double >
	{
		static TwType tw_type() { return TW_TYPE_DOUBLE; }

		static void Read(XMLElement * xml_element, double * value)
		{
			xml_element->QueryDoubleAttribute("value", value);
		}

		static void Write(XMLElement * xml_element, const char * name, const double * value)
		{
			XMLElement * new_element = xml_element->GetDocument()->NewElement(name);
			new_element->SetAttribute("type", "double");
			new_element->SetAttribute("value", *value);
			xml_element->InsertEndChild(new_element);
		}
	};

	// fully specialized read/write for std::string
	template<>
	struct AutoXmlType < std::string >
	{
		static TwType tw_type() { return TW_TYPE_STDSTRING; }

		static void Read(XMLElement * xml_element, std::string * value)
		{
			*value = xml_element->Attribute("text");
		}

		static void Write(XMLElement * xml_element, const char * name, const std::string * value)
		{
			XMLElement * new_element = xml_element->GetDocument()->NewElement(name);
			new_element->SetAttribute("type", "string");
			new_element->SetAttribute("text", value->c_str());
			xml_element->InsertEndChild(new_element);
		}
	};

	template<>
	struct AutoXmlType < glm::vec3 >
	{
		static TwType tw_type() { return TW_TYPE_DIR3F; }

		static void Read(XMLElement * xml_element, glm::vec3 * value)
		{
			xml_element->QueryFloatAttribute("x", &value->x);
			xml_element->QueryFloatAttribute("y", &value->y);
			xml_element->QueryFloatAttribute("z", &value->z);
		}

		static void Write(XMLElement * xml_element, const char * name, const glm::vec3 * value)
		{
			XMLElement * new_element = xml_element->GetDocument()->NewElement(name);
			new_element->SetAttribute("type", "vec3");
			new_element->SetAttribute("x", value->x);
			new_element->SetAttribute("y", value->y);
			new_element->SetAttribute("z", value->z);
			xml_element->InsertEndChild(new_element);
		}
	};

	template<>
	struct AutoXmlType < AColor3F >
	{
		static TwType tw_type() { return TW_TYPE_COLOR3F; }

		static void Read(XMLElement * xml_element, AColor3F * value)
		{
			xml_element->QueryFloatAttribute("r", &value->vec.x);
			xml_element->QueryFloatAttribute("g", &value->vec.y);
			xml_element->QueryFloatAttribute("b", &value->vec.z);
		}

		static void Write(XMLElement * xml_element, const char * name, const AColor3F * value)
		{
			XMLElement * new_element = xml_element->GetDocument()->NewElement(name);
			new_element->SetAttribute("type", "color3f");
			new_element->SetAttribute("r", value->vec.x);
			new_element->SetAttribute("g", value->vec.y);
			new_element->SetAttribute("b", value->vec.z);
			xml_element->InsertEndChild(new_element);
		}
	};

	template<>
	struct AutoXmlType < AColor4F >
	{
		static TwType tw_type() { return TW_TYPE_COLOR4F; }

		static void Read(XMLElement * xml_element, AColor4F * value)
		{
			xml_element->QueryFloatAttribute("r", &value->vec.x);
			xml_element->QueryFloatAttribute("g", &value->vec.y);
			xml_element->QueryFloatAttribute("b", &value->vec.z);
			xml_element->QueryFloatAttribute("a", &value->vec.w);
		}

		static void Write(XMLElement * xml_element, const char * name, const AColor4F * value)
		{
			XMLElement * new_element = xml_element->GetDocument()->NewElement(name);
			new_element->SetAttribute("type", "color4f");
			new_element->SetAttribute("r", value->vec.x);
			new_element->SetAttribute("g", value->vec.y);
			new_element->SetAttribute("b", value->vec.z);
			new_element->SetAttribute("a", value->vec.w);
			xml_element->InsertEndChild(new_element);
		}
	};
}