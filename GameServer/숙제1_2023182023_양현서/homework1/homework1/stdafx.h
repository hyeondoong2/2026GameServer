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

enum class PlayerMoveDir {
  kUp,
  kDown,
  kLeft,
  kRight
};

#endif