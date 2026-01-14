#pragma once
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <dwmapi.h>
#include <psapi.h>
#include <commoncontrols.h>
#include <iostream>
#include <vector>
#include <algorithm>
#include <string>
#include <cmath>
#include <fstream>
#include <shlobj.h>
#include <objbase.h>
#include <shlwapi.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <gdiplus.h>
#include <filesystem>
namespace fs = std::filesystem;

std::string GetProcessName(DWORD pid);