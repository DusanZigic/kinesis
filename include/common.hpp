#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <psapi.h>
#include <commoncontrols.h>
#include <shlobj.h>
#include <objbase.h>
#include <shlwapi.h>
#include <gdiplus.h>

#include <iostream>
#include <vector>
#include <set>
#include <algorithm>
#include <string>
#include <sstream>
#include <cmath>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>

std::string ToUpper(std::string s);
std::string ToLower(std::string s);
std::string GetProcessName(DWORD pid);
std::string GetKnownFolderPath(REFKNOWNFOLDERID rfid);