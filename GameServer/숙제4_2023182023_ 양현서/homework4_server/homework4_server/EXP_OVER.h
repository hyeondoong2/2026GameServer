#ifndef EXP_OVER_H_
#define EXP_OVER_H_

#include <winsock2.h>

#include <cstring>
#include <memory>
#include "Common.h"
#include "Session.h"

class EXP_OVER
{
public:
    WSAOVERLAPPED wsa_over_;
    size_t s_id_;
    WSABUF wsa_buf_;
    char send_msg_[BUFSIZE];

public:
    EXP_OVER() = default;

    // recv
    EXP_OVER(size_t s_id) : s_id_(s_id)
    {
        ZeroMemory(&wsa_over_, sizeof(wsa_over_));
        wsa_buf_.buf = send_msg_;
        wsa_buf_.len = BUFSIZE;
        wsa_over_.hEvent = reinterpret_cast<HANDLE>(static_cast<size_t>(s_id_));    // 콜백에서 어느 세션인지 찾기 위해...
    }

    // send
    EXP_OVER(size_t s_id, char num_bytes, char* mess) : s_id_(s_id)
    {
        ZeroMemory(&wsa_over_, sizeof(wsa_over_));
        wsa_buf_.buf = send_msg_;
        wsa_buf_.len = num_bytes;  
        memcpy(send_msg_, mess, num_bytes);
    }

    ~EXP_OVER() {}
};

#endif  // EXP_OVER_H_