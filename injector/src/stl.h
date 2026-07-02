#pragma once

// Standalone prelude for the ported exporter, standing in for GWToolbox's plugins/Base/stl.h.
// GameState.cpp and Exporter.cpp include it FIRST, before any GWCA header: GWCA's manager
// headers assume the common std headers + <Windows.h> are already visible (e.g. MemoryMgr.h
// uses std::function and HMODULE without including <functional>/<windows.h> itself). We mirror
// GWCA's own stdafx.h include set rather than GWToolbox's kitchen-sink prelude, so the injector
// tree stays free of GWToolbox headers (see INJECTOR.md "Remove GWToolboxpp references").

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

// windows first: GWCA headers reference HMODULE / HWND / DWORD unqualified.
#include <Windows.h>

// C++ style C headers
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

// C++ headers GWCA manager/entity headers assume are already included
#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
