#ifndef __CONFIG_HPP__
#define __CONFIG_HPP__

#include <string>
#include <vector>
#include <map>
#include <tuple>

struct CfgSection
{
	// unordered config entries, for general config stuff
	typedef std::map<std::string, std::string> Unordered;
	// config entires where order matters (for channel muting)
	typedef std::vector< std::pair<std::string, std::string> > Ordered;
	
	Unordered unord;
	Ordered ordered;
};

class Configuration
{
public:
	typedef std::map<std::string, CfgSection> SectList;
	
	SectList _sections;
	
	void AddEntry(const char* section, const char* key, const char* value, bool ordered = false);
	Configuration& operator+=(const Configuration& rhs);
	
	static std::string ToString(const std::string& text);
	static unsigned long ToUInt(const std::string& text);
	static long ToSInt(const std::string& text);
	static double ToFloat(const std::string& text);
	static bool ToBool(const std::string& text);
};

#endif	// __CONFIG_HPP__
