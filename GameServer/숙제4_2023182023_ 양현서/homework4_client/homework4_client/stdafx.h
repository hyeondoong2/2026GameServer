#ifndef STDAFX_H_
#define STDAFX_H_

#include "targetver.h"
#define WIN32_LEAN_AND_MEAN             

#include <windows.h>

#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <atlimage.h>

// C++
#include <memory>
#include <iostream>
#include <algorithm>

// Network
#include <WS2tcpip.h>
#pragma comment (lib, "WS2_32.LIB")  

static const char* SERVER_ADDR = "127.0.0.1";
static const short SERVER_PORT = 4000;
static const int BUFSIZE = 256;

#ifdef UNICODE
#pragma comment(linker, "/entry:wWinMainCRTStartup /subsystem:console") 
#else
#pragma comment(linker, "/entry:WinMainCRTStartup /subsystem:console") 
#endif

using namespace std;




#endif