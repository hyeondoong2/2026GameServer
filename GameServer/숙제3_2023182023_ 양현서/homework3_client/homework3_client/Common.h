#ifndef COMMON_H_  
#define COMMON_H_

#pragma pack(push, 1)

#include "Player.h"
#include <unordered_map>

enum class PlayerMoveDir : unsigned char {
  kNone = 0,
  kUp,
  kDown,
  kLeft,
  kRight
};



#pragma pack(pop)

#endif  // COMMON_H_