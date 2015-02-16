#pragma once

#include <iostream>
#include <vector>
#include <functional>
#include <assert.h>
#include "tinyxml2.h"

namespace cubey {
	using namespace tinyxml2;

	template<typename T>
	struct AutoXmlType
	{
		static void Read(XMLElement * xml_element, T & value)
		{
			std::cerr << "missing XML reading function for value type : " << typeid(T).name() << std::endl;
			assert(false);
		}

		static void Write(XMLElement * xml_element, const std::string & name, const T & value)
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

		static void Write(XMLElement * xml_element, const std::string & name, const std::vector<T> & value)
		{
			XMLElement * new_element = xml_element->GetDocument()->NewElement(name.c_str());
			for (auto & item : value)
			{
				VarType<T>::Write(new_element, "item", item);
			}
			xml_element->InsertEndChild(new_element);
		}
	};

	// fully specialized read/write for bool
	template<>
	struct AutoXmlType < bool >
	{
		static void Read(XMLElement * xml_element, bool * value)
		{
			xml_element->QueryBoolText(value);
		}

		static void Write(XMLElement * xml_element, const std::string & name, const bool & value)
		{
			XMLElement * new_element = xml_element->GetDocument()->NewElement(name.c_str());
			new_element->SetText(value);
			xml_element->InsertEndChild(new_element);
		}
	};

	// fully specialized read/write for int
	template<>
	struct AutoXmlType < int >
	{
		static void Read(XMLElement * xml_element, int * value)
		{
			xml_element->QueryIntText(value);
		}

		static void Write(XMLElement * xml_element, const std::string & name, const int & value)
		{
			XMLElement * new_element = xml_element->GetDocument()->NewElement(name.c_str());
			new_element->SetText(value);
			xml_element->InsertEndChild(new_element);
		}
	};

	// fully specialized read/write for float
	template<>
	struct AutoXmlType < float >
	{
		static void Read(XMLElement * xml_element, float * value)
		{
			xml_element->QueryFloatText(value);
		}

		static void Write(XMLElement * xml_element, const std::string & name, const float & value)
		{
			XMLElement * new_element = xml_element->GetDocument()->NewElement(name.c_str());
			new_element->SetText(value);
			xml_element->InsertEndChild(new_element);
		}
	};

	// fully specialized read/write for double
	template<>
	struct AutoXmlType < double >
	{
		static void Read(XMLElement * xml_element, double * value)
		{
			xml_element->QueryDoubleText(value);
		}

		static void Write(XMLElement * xml_element, const std::string & name, const double & value)
		{
			XMLElement * new_element = xml_element->GetDocument()->NewElement(name.c_str());
			new_element->SetText(value);
			xml_element->InsertEndChild(new_element);
		}
	};

	// fully specialized read/write for std::string
	template<>
	struct AutoXmlType < std::string >
	{
		static void Read(XMLElement * xml_element, std::string * value)
		{
			*value = xml_element->GetText();
		}

		static void Write(XMLElement * xml_element, const std::string & name, const std::string & value)
		{
			XMLElement * new_element = xml_element->GetDocument()->NewElement(name.c_str());
			new_element->SetText(value.c_str());
			xml_element->InsertEndChild(new_element);
		}
	};
}