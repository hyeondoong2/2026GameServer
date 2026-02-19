#ifndef COMMON_H_  
#define COMMON_H_

#pragma pack(push, 1)

enum class PlayerMoveDir : unsigned char {
  kNone = 0,
  kUp,
  kDown,
  kLeft,
  kRight
};

struct KeyPacket {
  PlayerMoveDir move_dir;
};

struct PosPacket {
  int x;
  int y;
};

#pragma pack(pop)

#endif  // COMMON_H_