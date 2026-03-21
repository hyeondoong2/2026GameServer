#ifndef COMMON_H_  
#define COMMON_H_

const short SERVER_PORT = 4000;
const int BUFSIZE = 256;

enum class PlayerMoveDir : unsigned char
{
    kNone = 0,
    kUp,
    kDown,
    kLeft,
    kRight
};

#endif  // COMMON_H_