// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once
#include "targetver.h"

#include "windows.h"
#include <process.h> 
#include <tchar.h>	// _T() macro. Some API functions require unicode strings

// crt
#include <ctime>
#include <assert.h>

// windef.h defines these macros which conflict with stl functions
#ifdef max 
#undef max
#endif

#ifdef min
#undef min
#endif

// stl: include before redefining of "new" operator
#include <string>
#include <iostream>
#include <sstream>
#include <limits>

using std::cout;
using std::cin;
using std::endl;
using std::stringstream;

// detecting memory leaks ( included auto only in _DEBUG )
#define _CRT_DBG_MAP_ALLOCK
#include <crtdbg.h>

// show the file and line number in debug output
#ifdef _DEBUG
#define new new(_NORMAL_BLOCK, __FILE__, __LINE__)
#endif
