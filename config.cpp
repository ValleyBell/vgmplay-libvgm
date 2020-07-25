#include <string>
#include <vector>
#include <map>
#include <tuple>
#include <algorithm>	// for std::transform
#include <cctype>	// for tolower()

#include "config.hpp"

void Configuration::AddEntry(const char* section, const char* key, const char* value, bool ordered)
{
	SectList::iterator lsIt;
	
	lsIt = _sections.find(section);
	if (lsIt == _sections.end())
		lsIt = _sections.insert(std::pair<std::string, CfgSection>(section, CfgSection())).first;
	
	if (! ordered)
		lsIt->second.unord[key] = value;
	else
		lsIt->second.ordered.push_back(std::pair<std::string, std::string>(key, value));
	
	return;
}

Configuration& Configuration::operator+=(const Configuration& rhs)
{
	// section iterators
	SectList::iterator lsIt;
	SectList::const_iterator rsIt;
	
	for (rsIt = rhs._sections.begin(); rsIt != rhs._sections.end(); ++rsIt)
	{
		lsIt = _sections.find(rsIt->first);
		if (lsIt == _sections.end())
		{
			_sections.insert(*rsIt);
		}
		else
		{
			// entry iterators
			CfgSection::Unordered::const_iterator rueIt;
			CfgSection::Ordered::const_iterator roeIt;
			
			for (rueIt = rsIt->second.unord.begin(); rueIt != rsIt->second.unord.end(); ++rueIt)
				lsIt->second.unord[rueIt->first] = rueIt->second;
			for (roeIt = rsIt->second.ordered.begin(); roeIt != rsIt->second.ordered.end(); ++roeIt)
				lsIt->second.ordered.push_back(*roeIt);
		}
	}
	
	return *this;
}

/*static*/ std::string Configuration::ToString(const std::string& text)
{
	std::string valStr;
	unsigned char state = 0x00;
	size_t curPos;
	
	// remove quotation marks - unless they are escaped with \"
	valStr.reserve(text.size());
	for (curPos = 0; curPos < valStr.length(); curPos ++)
	{
		if (state & 0x02)
		{
			state &= ~0x02;
			valStr.push_back(text[curPos]);
		}
		else if (text[curPos] == '\\')
		{
			state |= 0x02;
		}
		else if (text[curPos] == '\"')
		{
			state ^= 0x01;
			if (! (state & 0x01))
				break;	// stop parsing after closing quotation mark
		}
		else
		{
			valStr.push_back(text[curPos]);
		}
	}
	return valStr;
}

/*static*/ unsigned long Configuration::ToUInt(const std::string& text)
{
	char* end;
	unsigned long value = strtoul(text.c_str(), &end, 0);	// strtoul instead of atoi so that hex works as well
	return (end > text.c_str()) ? value : 0;
}

/*static*/ long Configuration::ToSInt(const std::string& text)
{
	char* end;
	long value = strtol(text.c_str(), &end, 0);	// strtol instead of atoi so that hex works as well
	return (end > text.c_str()) ? value : 0;
}

/*static*/ double Configuration::ToFloat(const std::string& text)
{
	char* end;
	double value = strtod(text.c_str(), &end);
	return (end > text.c_str()) ? value : 0.0;
}

/*static*/ bool Configuration::ToBool(const std::string& text)
{
	// based on inih/cpp/INIReader.cpp
	std::string lowerText = text;
	std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);	// for case-insensitive comparison
	if (lowerText == "true" || lowerText == "yes" || lowerText == "on")
		return true;
	else if (lowerText == "false" || lowerText == "no" || lowerText == "off")
		return false;
	else
		return (ToSInt(text) != 0);
}
