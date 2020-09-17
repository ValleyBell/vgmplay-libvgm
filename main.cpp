#include <stdio.h>
#include <stdlib.h>
#include <string.h>	// for memcmp()
#include <ctype.h>
#include <locale.h>
#include <errno.h>
#include <vector>
#include <string>
#include <map>
#include <sstream>
#include <algorithm>

#ifdef _MSC_VER
#define stricmp		_stricmp
#define strnicmp	_strnicmp
#else
#define stricmp		strcasecmp
#define strnicmp	strncasecmp
#endif

#ifdef _WIN32
#include <conio.h>
#include <Windows.h>

#define USE_WMAIN
#else
#include <unistd.h>
#define Sleep(x)	usleep(x * 1000)
#include <limits.h>	// for PATH_MAX
#include <signal.h>	// for kill()
#endif

#include <ini.h>
#include <getopt.h>

#include "stdtype.h"
#include "utils.hpp"
#include "m3uargparse.hpp"
#include "config.hpp"

#ifndef SHARE_PREFIX
#define SHARE_PREFIX	"/usr"
#endif


extern UINT8 PlayerMain(void);


struct OptionItem
{
	unsigned char flags;    // 0 - no parameter, 1 - has 1 parameter
	char shortOpt;          // character for short option ['\0' = no short option]
	const char* longOpt;    // word for long option
	const char* paramName;  // [optional] parameter name for "help" screen
	const char* helpText;
};
typedef std::vector<OptionItem> OptionList;


static char* GetAppFilePath(void);
static void InitAppSearchPaths(const char* argv_0);
static int IniValHandler(void* user, const char* section, const char* name, const char* value);
static UINT8 LoadConfig(const std::string& iniPath, Configuration& cfg);
static std::string GenerateOptData(const OptionList& optList, std::vector<struct option>* longOpts);
static int ParseArguments(int argc, char* argv[], Configuration& argCfg);


static const OptionList optionList =
{
	{0, 'h', "help",            NULL,     "show this help screen"},
	{0, 'v', "version",         NULL,     "show version"},
	{0, 'w', "dump-wav",        NULL,     "enable WAV dumping"},
	{1, 'd', "output-device",   "id",     "output device ID"},
	{1, 'c', "config",          "option", "set configuration option, format: section.key=Data"},
};


static std::vector<std::string> appSearchPaths;
static std::vector<std::string> cfgFileNames;
       Configuration playerCfg;

       std::vector<SongFileList> songList;
       std::vector<std::string> plList;

#ifdef USE_WMAIN
int wmain(int argc, wchar_t* wargv[])
{
	char** argv;
#else
int main(int argc, char* argv[])
{
#endif
	int argbase;
	UINT8 retVal;
	//int resVal;
	Configuration argCfg;
	
	setlocale(LC_ALL, "");	// enable UTF-8 support on Linux
	setlocale(LC_NUMERIC, "C");	// enforce decimal dot
	
	printf("VGM Player\n");
	printf("----------\n");
	if (argc < 2)
	{
#ifdef USE_WMAIN
		printf("Usage: %ls [options] file1.vgm file2.vgz ...\n", wargv[0]);
#else
		printf("Usage: %s [options] file1.vgm file2.vgz ...\n", argv[0]);
#endif
		//printf("Options:\n");
		//printf("    none currently\n");
		return 0;
	}
	
#ifdef USE_WMAIN
	argv = (char**)malloc(argc * sizeof(char*));
	for (argbase = 0; argbase < argc; argbase ++)
	{
		int bufSize = WideCharToMultiByte(CP_UTF8, 0, wargv[argbase], -1, NULL, 0, NULL, NULL);
		argv[argbase] = (char*)malloc(bufSize);
		WideCharToMultiByte(CP_UTF8, 0, wargv[argbase], -1, argv[argbase], bufSize, NULL, NULL);
	}
	// Note: I'm not freeing argv anywhere. I'll let Windows take care about it this one time.
#endif
	
	argbase = ParseArguments(argc, argv, argCfg);
	if (argbase == 0)
		return 0;
	else if (argbase < 0)
		return 1;
	if (argc < argbase + 1)
	{
		printf("Not enough arguments.\n");
		return 1;
	}
	
	InitAppSearchPaths(argv[0]);
	cfgFileNames.push_back("VGMPlay.ini");
	cfgFileNames.push_back("vgmplay.ini");
	
	std::string cfgFilePath = FindFile_List(cfgFileNames, appSearchPaths);
	if (cfgFilePath.empty())
	{
		printf("%s not found!\n", cfgFileNames[cfgFileNames.size() - 1].c_str());
		return 1;
	}
	
	// TODO: load default configuration here?
	LoadConfig(cfgFilePath, playerCfg);	// load INI file
	playerCfg += argCfg;	// override INI settings with commandline options
#if 0	// print current configuration
	{
		printf("Config File:\n");
		for (auto& cfgSect : playerCfg._sections)
		{
			printf("[%s]\n", cfgSect.first.c_str());
			for (auto& uEnt : cfgSect.second.unord)
				printf("\t.%s = %s\n", uEnt.first.c_str(), uEnt.second.c_str());
			for (auto& oEnt : cfgSect.second.ordered)
				printf("\t+%s = %s\n", oEnt.first.c_str(), oEnt.second.c_str());
		}
	}
#endif
	
	retVal = ParseSongFiles(std::vector<const char*>(argv + argbase, argv + argc), songList, plList);
	if (retVal)
		printf("One or more playlists couldn't be read!\n");
	if (songList.empty())
	{
		printf("No songs to play.\n");
		return 0;
	}
	
	retVal = PlayerMain();
	printf("Done.\n");
	
	return 0;
}

static char* GetAppFilePath(void)
{
	char* appPath;
	int retVal;
	
#ifdef _WIN32
#ifdef USE_WMAIN
	std::vector<wchar_t> appPathW;
	
	appPathW.resize(MAX_PATH);
	retVal = GetModuleFileNameW(NULL, &appPathW[0], appPathW.size());
	if (! retVal)
		appPathW[0] = L'\0';
	
	retVal = WideCharToMultiByte(CP_UTF8, 0, &appPathW[0], -1, NULL, 0, NULL, NULL);
	if (retVal < 0)
		retVal = 1;
	appPath = (char*)malloc(retVal);
	retVal = WideCharToMultiByte(CP_UTF8, 0, &appPathW[0], -1, appPath, retVal, NULL, NULL);
	if (retVal < 0)
		appPath[0] = '\0';
	appPathW.clear();
#else
	appPath = (char*)malloc(MAX_PATH * sizeof(char));
	retVal = GetModuleFileNameA(NULL, appPath, MAX_PATH);
	if (! retVal)
		appPath[0] = '\0';
#endif
#else
	appPath = (char*)malloc(PATH_MAX * sizeof(char));
	retVal = readlink("/proc/self/exe", appPath, PATH_MAX);
	if (retVal == -1)
		appPath[0] = '\0';
#endif
	
	return appPath;
}

static void InitAppSearchPaths(const char* argv_0)
{
	appSearchPaths.clear();
	
#ifndef WIN32
	// 1. [Unix only] global share directory
	appSearchPaths.push_back(SHARE_PREFIX "/share/vgmplay/");
#endif
	
	// 2. actual application path (potentially resolved symlink)
	char* appPath = GetAppFilePath();
	const char* appTitle = GetFileTitle(appPath);
	if (appTitle != appPath)
		appSearchPaths.push_back(std::string(appPath, appTitle - appPath));
	free(appPath);
	
	// 3. called path
	appTitle = GetFileTitle(argv_0);
	if (appTitle != argv_0)
	{
		std::string callPath(argv_0, appTitle - argv_0);
		if (callPath != appSearchPaths[appSearchPaths.size() - 1])
			appSearchPaths.push_back(callPath);
	}
	
	// 4. home/config directory
	std::string cfgDir;
#ifdef WIN32
	cfgDir = getenv("USERPROFILE");
	cfgDir += "/.vgmplay/";
#else
	char* xdgPath = getenv("XDG_CONFIG_HOME");
	if (xdgPath != NULL && xdgPath[0] != '\0')
	{
		cfgDir = xdgPath;
	}
	else
	{
		cfgDir = getenv("HOME");
		cfgDir += "/.config";
	}
	cfgDir += "/vgmplay/";
#endif
	appSearchPaths.push_back(cfgDir);
	
	// 5. working directory
	appSearchPaths.push_back("./");
	
	return;
}

static int IniValHandler(void* user, const char* section, const char* name, const char* value)
{
	Configuration* cfg = (Configuration*)user;
	
	bool ordered = false;
	if (! strnicmp(name, "Mute", 4))
		ordered = true;
	
	cfg->AddEntry(section, name, value, ordered);
	
	return 1;
}

static UINT8 LoadConfig(const std::string& iniPath, Configuration& cfg)
{
	int retVal;
	
	retVal = ini_parse(iniPath.c_str(), IniValHandler, &cfg);
	if (retVal == -2)
		return 0xF8;	// malloc error
	else if (retVal == -1)
		return 0xF0;	// file not found
	else if (retVal < 0)
		return 0xFF;	// unknown error
	else if (retVal > 0)
		return 0x01;	// parse error
	else
		return 0x00;
}

static std::string GenerateOptData(const OptionList& optList, std::vector<struct option>* longOpts)
{
	size_t curOpt;
	std::string shortOpts;
	
	if (longOpts != NULL)
		longOpts->resize(optList.size() + 1);
	
	shortOpts = "";
	for (curOpt = 0; curOpt < optList.size(); curOpt ++)
	{
		const auto& optDef = optList[curOpt];
		
		if (optDef.shortOpt != '\0')
		{
			shortOpts.push_back(optDef.shortOpt);
			if (optDef.flags & 0x01)
				shortOpts.push_back(':');
		}
		
		if (longOpts != NULL)
		{
			struct option& lOpt = (*longOpts)[curOpt];
			
			lOpt.name = optDef.longOpt;
			lOpt.val = optDef.shortOpt;
			lOpt.flag = NULL;
			lOpt.has_arg = (optDef.flags & 0x01) ? required_argument : no_argument;
		}
	}
	
	if (longOpts != NULL)
	{
		// write terminator option
		auto& lOptTerm = (*longOpts)[optList.size()];
		memset(&lOptTerm, 0x00, sizeof(struct option));
	}
	
	return shortOpts;
}

static void PrintVersion(void)
{
	printf("VGMPlay %s, supports VGM %s\n", "0.5.0", "1.71b");
	return;
}

static void PrintArgumentHelp(void)
{
	std::vector<std::string> cmdCol;	// command column
	const int indent = 4;
	size_t maxCmdLen;
	size_t curOpt;
	
	cmdCol.resize(optionList.size());
	maxCmdLen = 0;
	for (curOpt = 0; curOpt < optionList.size(); curOpt ++)
	{
		const OptionItem& oItm = optionList[curOpt];
		std::string& cmdStr = cmdCol[curOpt];
		
		cmdStr = std::string("-") + oItm.shortOpt + ", --" + oItm.longOpt;
		if ((oItm.flags & 0x01) && oItm.paramName != NULL)
			cmdStr = cmdStr + " " + oItm.paramName;
		
		if (maxCmdLen < cmdStr.length())
			maxCmdLen = cmdStr.length();
	}
	maxCmdLen += 2;	// add 2 characters of padding
	maxCmdLen = (maxCmdLen + 3) & ~3;	// round up to 4
	
	for (curOpt = 0; curOpt < optionList.size(); curOpt ++)
	{
		const auto& oItm = optionList[curOpt];
		int padding = static_cast<int>(maxCmdLen - cmdCol[curOpt].length());
		printf("%*s%s%*s%s\n", indent, "", cmdCol[curOpt].c_str(), padding, "  ", oItm.helpText);
	}
	
	return;
}

static int ParseArguments(int argc, char* argv[], Configuration& argCfg)
{
	std::string sOpts;
	std::vector<struct option> lOpts;
	
	sOpts = GenerateOptData(optionList, &lOpts);
	optind = 1;
	while(true)
	{
		int retVal = getopt_long(argc, argv, sOpts.c_str(), &lOpts[0], NULL);
		if (retVal == -1)
			break;	// finished argument parsing
		else if (retVal == '?')
			return -1;	// getopt already prints a message by default, so just return
		
		switch(retVal)
		{
		case 'v':	// version
			PrintVersion();
			return 0;
		case 'h':	// help
			PrintVersion();
			printf("Usage: %s [options] file1.vgm [file2.vgz] [...]\n", argv[0]);
			PrintArgumentHelp();
			return 0;
		case 'w':	// dump-wav
			argCfg.AddEntry("General", "LogSound", "1");
			break;
		case 'd':	// output-device
			argCfg.AddEntry("General", "OutputDevice", optarg);
			break;
		case 'c':	// configuration setting
			{
				std::string optstr = optarg;
				char* sect = &optstr[0];
				char* key = strchr(sect, '.');
				if (key == NULL)
					break;
				*key = '\0';	key ++;
				char* val = strchr(key, '=');
				if (val == NULL)
					break;
				*val = '\0';	val ++;
				// reuse INI handler, so that the MuteMask values are put into the correct section
				IniValHandler(&argCfg, sect, key, val);
			}
			break;
		}
	}
	
	return optind;
}
