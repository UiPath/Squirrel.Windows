// StubExecutable.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "StubExecutable.h"
#include <shellapi.h>
#include <queue>
#include <VersionHelpers.h>

#include "semver200.h"

using namespace std;

wchar_t* FindRootAppDir() 
{
	wchar_t* ourDirectory = new wchar_t[MAX_PATH];

	GetModuleFileName(GetModuleHandle(NULL), ourDirectory, MAX_PATH);
	wchar_t* lastSlash = wcsrchr(ourDirectory, L'\\');
	if (!lastSlash) {
		delete[] ourDirectory;
		return NULL;
	}

	// Null-terminate the string at the slash so now it's a directory
	*lastSlash = 0x0;
	return ourDirectory;
}

wchar_t* FindOwnExecutableName() 
{
	wchar_t* ourDirectory = new wchar_t[MAX_PATH];

	GetModuleFileName(GetModuleHandle(NULL), ourDirectory, MAX_PATH);
	wchar_t* lastSlash = wcsrchr(ourDirectory, L'\\');
	if (!lastSlash) {
		delete[] ourDirectory;
		return NULL;
	}

	wchar_t* ret = _wcsdup(lastSlash + 1);
	delete[] ourDirectory;
	return ret;
}

bool IsMachineSupported()
{
	// 32 bit not supported.
	SYSTEM_INFO si { 0 };
	GetNativeSystemInfo(&si);
	if (((si.wProcessorArchitecture & PROCESSOR_ARCHITECTURE_IA64) != PROCESSOR_ARCHITECTURE_IA64) &&
		((si.wProcessorArchitecture & PROCESSOR_ARCHITECTURE_AMD64) != PROCESSOR_ARCHITECTURE_AMD64))
	{
		return false;
	}

	//Windows 7 or lower not supported.
	return IsWindows8OrGreater();
}

void DeleteDirectory(const std::wstring& path)
{
	int len = path.length() + 2; // required to set 2 nulls at end of argument to SHFileOperation.
	std::vector<wchar_t> tempdir;
	tempdir.resize(len);
	path.copy(&tempdir[0], path.length());

	SHFILEOPSTRUCT file_op = {
	   NULL,
	   FO_DELETE,
	   &tempdir[0],
	   NULL,
	   FOF_NOCONFIRMATION |
	   FOF_NOERRORUI |
	   FOF_SILENT,
	   false,
	   0,
	   NULL };
	SHFileOperation(&file_op);
}

void DeleteUnsupportedDirs(const std::wstring& searchPattern)
{
	//searchPattern: C:\Users\xxx\AppData\Local\UiPath\app-*
	WIN32_FIND_DATA fileInfo = { 0 };
	HANDLE hFile = FindFirstFile(searchPattern.c_str(), &fileInfo);
	if (hFile == INVALID_HANDLE_VALUE) {
		return;
	}

	version::Semver200_version thresholdVersion("21.0.0");

	do {
		std::wstring directoryName = fileInfo.cFileName;
		if (!(fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			continue;
		}
		std::wstring fullDirectoryPath = searchPattern.substr(0, searchPattern.length() - 5) + directoryName;
		std::wstring currentVersionName = directoryName.substr(4);   // Skip 'app-'

		version::Semver200_version currentVersion(std::string(currentVersionName.begin(), currentVersionName.end()));

		if (currentVersion > thresholdVersion)
		{
			// delete all directories greater than app-21.
			DeleteDirectory(fullDirectoryPath);
		}
	} while (FindNextFile(hFile, &fileInfo));

	FindClose(hFile);
}

std::wstring FindLatestAppDir() 
{
	std::wstring ourDir;
	ourDir.assign(FindRootAppDir());
	ourDir += L"\\app-*";
	
	if (!IsMachineSupported())
	{
		DeleteUnsupportedDirs(ourDir);
	}

	WIN32_FIND_DATA fileInfo = { 0 };
	HANDLE hFile = FindFirstFile(ourDir.c_str(), &fileInfo);
	if (hFile == INVALID_HANDLE_VALUE) {
		return NULL;
	}

	version::Semver200_version acc("0.0.0");
	std::wstring acc_s;

	do {
		std::wstring appVer = fileInfo.cFileName;
		appVer = appVer.substr(4);   // Skip 'app-'
		if (!(fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
			continue;
		}

		std::string s(appVer.begin(), appVer.end());

		version::Semver200_version thisVer(s);

		if (thisVer > acc) {
			acc = thisVer;
			acc_s = appVer;
		}
	} while (FindNextFile(hFile, &fileInfo));

	if (acc == version::Semver200_version("0.0.0")) {
		return NULL;
	}

	ourDir.assign(FindRootAppDir());
	std::wstringstream ret;
	ret << ourDir << L"\\app-" << acc_s;

	FindClose(hFile);
	return ret.str();
}

std::wstring FindRealAppDir(const std::wstring& appdir, const std::wstring& appName)
{
	std::queue<std::wstring> directories;
	directories.push(appdir);

	while (!directories.empty())
	{
		WIN32_FIND_DATA fileInfo = { 0 };
		const std::wstring& searchDir = directories.front();			//current search directory
		const std::wstring search = searchDir + L"\\*";				//current search string (all)
		const std::wstring match = searchDir + L"\\" + appName;			//current desired match
		HANDLE hFile = FindFirstFile(search.c_str(), &fileInfo);

		if (hFile == INVALID_HANDLE_VALUE)
		{
			directories.pop();
			continue;
		}

		do
		{
			const std::wstring crtFile = std::wstring(fileInfo.cFileName);
			const std::wstring crtFileFull = searchDir + L"\\" + crtFile;
			if (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				if (crtFile.compare(L".") == 0 || crtFile.compare(L"..") == 0)
					continue;
				directories.push(crtFileFull);
			}
			else if (match.compare(crtFileFull) == 0)
			{
				FindClose(hFile);
				return searchDir;
			}
		} while (FindNextFile(hFile, &fileInfo) != 0);

		//move to nextDir
		FindClose(hFile);
		directories.pop();
	}

	// odd that we didn't find it, revert to squirell error management.
	return appdir;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
	std::wstring appName;
	appName.assign(FindOwnExecutableName());

	const std::wstring workingDir(FindLatestAppDir());
	const std::wstring realWorkingDir(FindRealAppDir(workingDir, appName));
	const std::wstring fullPath(realWorkingDir + L"\\" + appName);

	STARTUPINFO si = { 0 };
	PROCESS_INFORMATION pi = { 0 };

	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = nCmdShow;

	std::wstring cmdLine(L"\"");
	cmdLine += fullPath;
	cmdLine += L"\" ";
	cmdLine += lpCmdLine;

	wchar_t* lpCommandLine = wcsdup(cmdLine.c_str());
	wchar_t* lpCurrentDirectory = wcsdup(realWorkingDir.c_str());
	if (!CreateProcess(NULL, lpCommandLine, NULL, NULL, true, 0, NULL, lpCurrentDirectory, &si, &pi)) {
		return -1;
	}

	AllowSetForegroundWindow(pi.dwProcessId);
	WaitForInputIdle(pi.hProcess, 5 * 1000);
	return 0;
}
