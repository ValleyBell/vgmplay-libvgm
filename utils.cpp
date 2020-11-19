#include <stddef.h>
#include <string.h>
#include <string>
#include <stdio.h>
#include <vector>
#include <stdarg.h>

#include <Windows.h>	// for WriteConsoleW etc.

#include "utils.hpp"


static const char* GetLastDirSeparator(const char* filePath);
//const char* GetFileTitle(const char* filePath);
//const char* GetFileExtension(const char* filePath);
//void StandardizeDirSeparators(std::string& filePath);
static bool IsAbsolutePath(const char* filePath);
//std::string CombinePaths(const std::string& basePath, const std::string& addPath);
//std::string FindFile_List(const std::vector<std::string>& fileList, const std::vector<std::string>& pathList);
//std::string FindFile_Single(const std::string& fileName, const std::vector<std::string>& pathList);
//std::string Vector2String(const std::vector<char>& data, size_t startPos, size_t endPos);
//std::string Vector2String(const std::vector<unsigned char>& data, size_t startPos, size_t endPos);
static size_t utf8_chrsize(const char* utf8chr);
static bool utf8_advance(const char** str);
//size_t utf8strlen(const char* str);
//char* utf8strseek(const char* str, size_t numChars);
//void RemoveControlChars(std::string& str);
//void RemoveQuotationMarks(std::string& str, char quotMark);
//void u8printf(const char* format, ...);


static const char* GetLastDirSeparator(const char* filePath)
{
	const char* sepPos1;
	const char* sepPos2;
	
	if (strncmp(filePath, "\\\\", 2))
		filePath += 2;	// skip Windows network prefix
	sepPos1 = strrchr(filePath, '/');
	sepPos2 = strrchr(filePath, '\\');
	if (sepPos1 == NULL)
		return sepPos2;
	else if (sepPos2 == NULL)
		return sepPos1;
	else
		return (sepPos1 < sepPos2) ? sepPos2 : sepPos1;
}

const char* GetFileTitle(const char* filePath)
{
	const char* dirSepPos;
	
	dirSepPos = GetLastDirSeparator(filePath);
	return (dirSepPos != NULL) ? &dirSepPos[1] : filePath;
}

const char* GetFileExtension(const char* filePath)
{
	const char* dirSepPos;
	const char* extDotPos;
	
	dirSepPos = GetLastDirSeparator(filePath);
	if (dirSepPos == NULL)
		dirSepPos = filePath;
	extDotPos = strrchr(dirSepPos, '.');
	return (extDotPos == NULL) ? NULL : (extDotPos + 1);
}

void StandardizeDirSeparators(std::string& filePath)
{
	size_t curChr;
	
	curChr = 0;
	if (! filePath.compare(curChr, 2, "\\\\"))
		curChr += 2;	// skip Windows network prefix
	for (; curChr < filePath.length(); curChr ++)
	{
		if (filePath[curChr] == '\\')
			filePath[curChr] = '/';
	}
	
	return;
}

static bool IsAbsolutePath(const char* filePath)
{
	if (filePath[0] == '\0')
		return false;	// empty string
	if (filePath[0] == '/' || filePath[0] == '\\')
		return true;	// absolute UNIX path / device-relative Windows path
	if (filePath[1] == ':')
	{
		if ((filePath[0] >= 'A' && filePath[0] <= 'Z') ||
			(filePath[0] >= 'a' && filePath[0] <= 'z'))
			return true;	// Windows device path: C:\path
	}
	if (! strncmp(filePath, "\\\\", 2))
		return true;	// Windows network path: \\computername\path
	return false;
}

std::string CombinePaths(const std::string& basePath, const std::string& addPath)
{
	if (basePath.empty() || IsAbsolutePath(addPath.c_str()))
		return addPath;
	char lastChr = basePath[basePath.length() - 1];
	if (lastChr == '/' || lastChr == '\\')
		return basePath + addPath;
	else
		return basePath + '/' + addPath;
}

std::string FindFile_List(const std::vector<std::string>& fileList, const std::vector<std::string>& pathList)
{
	std::vector<std::string>::const_reverse_iterator pathIt;
	std::vector<std::string>::const_reverse_iterator fileIt;
	std::string fullName;
	FILE* hFile;
	
	for (pathIt = pathList.rbegin(); pathIt != pathList.rend(); ++pathIt)
	{
		for (fileIt = fileList.rbegin(); fileIt != fileList.rend(); ++fileIt)
		{
			fullName = CombinePaths(*pathIt, *fileIt);
			//printf("Testing path: %s ...\n", fullName.c_str());
			
			hFile = fopen(fullName.c_str(), "r");
			if (hFile != NULL)
			{
				fclose(hFile);
				//printf("Success.\n");
				return fullName;
			}
		}
	}
	
	return "";
}

std::string FindFile_Single(const std::string& fileName, const std::vector<std::string>& pathList)
{
	std::vector<std::string> fileList;
	
	fileList.push_back(fileName);
	return FindFile_List(fileList, pathList);
}

std::string Vector2String(const std::vector<char>& data, size_t startPos, size_t endPos)
{
	if (endPos == std::string::npos)
		endPos = data.size();
	if (endPos < startPos)
		return std::string();
	const char* basePtr = &(*data.begin());
	return std::string(basePtr + startPos, basePtr + endPos);
}

std::string Vector2String(const std::vector<unsigned char>& data, size_t startPos, size_t endPos)
{
	if (endPos == std::string::npos)
		endPos = data.size();
	if (endPos <= startPos)
		return std::string();
	const unsigned char* basePtr = &(*data.begin());
	return std::string((const char*)basePtr + startPos, (const char*)basePtr + endPos);
}

// actually "Array2String", but it's convenient to have it called this way
std::string Vector2String(const unsigned char* data, size_t startPos, size_t endPos)
{
	if (endPos <= startPos)
		return std::string();
	return std::string((const char*)data + startPos, (const char*)data + endPos);
}

static size_t utf8_chrsize(const char* utf8chr)
{
	unsigned char ctrl = (unsigned char)utf8chr[0];
	
	if (ctrl < 0x80)
		return 1;
	else if (ctrl < 0xC2)
		return 0;	// invalid
	else if (ctrl < 0xE0)
		return 2;
	else if (ctrl < 0xF0)
		return 3;
	else if (ctrl < 0xF8)
		return 4;
	else if (ctrl < 0xFC)
		return 5;
	else if (ctrl < 0xFE)
		return 6;
	else
		return 0;	// invalid
}

static bool utf8_advance(const char** str)
{
	size_t chrBytes = utf8_chrsize(*str);
	// return true -> byte sequence was valid
	if (chrBytes <= 1)
	{
		// we advance by 1 byte for an invalid character
		(*str) ++;
		return (chrBytes == 1);
	}
	else
	{
		// we stop on the first invalid character
		while(chrBytes > 0 && (**str & 0x80))
		{
			(*str) ++;
			chrBytes --;
		}
		return (chrBytes == 0);
	}
}

size_t utf8strlen(const char* str)
{
	size_t chrCount;
	
	for (chrCount = 0; *str != '\0'; chrCount ++)
		utf8_advance(&str);
	
	return chrCount;
}

char* utf8strseek(const char* str, size_t numChars)
{
	size_t curChr;
	
	for (curChr = 0; curChr < numChars; curChr ++)
	{
		if (*str == '\0')
			break;
		utf8_advance(&str);
	}
	
	return (char*)str;
}

int count_digits(int value)
{
	int digits = 0;

	do
	{
		value /= 10;
		digits++;
	} while (value > 0);

	return digits;
}

void RemoveControlChars(std::string& str)
{
	size_t strLen = str.length();
	
	while(strLen > 0 && (unsigned char)str[strLen - 1] < 0x20)
		strLen --;
	str.resize(strLen);
	
	return;
}

void RemoveQuotationMarks(std::string& str, char quotMark)
{
	if (str.empty())
		return;
	if (str[0] != quotMark)
		return;
	
	size_t lastQmPos = str.rfind(quotMark);
	if (lastQmPos == std::string::npos)
		lastQmPos = str.length();
	str = str.substr(1, lastQmPos - 1);
	
	return;
}

// print UTF-8 string with correct codepage on Windows
#ifndef _WIN32
void u8printf(const char* format, ...)
{
	va_list arg_list;

	va_start(arg_list, format);
	vprintf(format, arg_list);
	va_end(arg_list);

	return;
}
#else
void u8printf(const char* format, ...)
{
	va_list arg_list;
	int retVal;
	BOOL retValB;
	int bufSize;
	std::string printBuf;
	std::wstring printWBuf;
	DWORD dummyDW;
	
	do
	{
		printBuf.resize(printBuf.size() + 0x100);
		
		// Note: On Linux every vprintf call needs its own set of va_start/va_end commands.
		//       Under Windows (with VC6) one only one block for all calls works, too.
		va_start(arg_list, format);
		retVal = _vsnprintf(&printBuf[0], printBuf.size(), format, arg_list);
		va_end(arg_list);
	} while(retVal == -1 && printBuf.size() < 0x1000);
	if (retVal >= 0)
		printBuf.resize(retVal);	// resize to "number of characters written"
	
	bufSize = MultiByteToWideChar(CP_UTF8, 0, printBuf.c_str(), printBuf.size(), NULL, 0);
	printWBuf.resize(bufSize);
	MultiByteToWideChar(CP_UTF8, 0, printBuf.c_str(), printBuf.size(), &printWBuf[0], bufSize);
	
	// This is the only way to print Unicode stuff to the Windows console.
	// No, wprintf doesn't work.
	retValB = WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), printWBuf.c_str(), printWBuf.length(), &dummyDW, NULL);
	if (! retValB)	// call failed (e.g. with ERROR_CALL_NOT_IMPLEMENTED on Win95)
	{
		// fallback to printf with OEM codepage
		UINT CPMode = GetConsoleOutputCP();
		bufSize = WideCharToMultiByte(CPMode, 0x00, printWBuf.c_str(), printWBuf.length(), NULL, 0, NULL, NULL);
		printBuf.resize(bufSize);
		WideCharToMultiByte(CPMode, 0x00, printWBuf.c_str(), printWBuf.length(), &printBuf[0], bufSize, NULL, NULL);
		
		puts(printBuf.c_str());
	}
	return;
}
#endif	// defined(_WIN32)
