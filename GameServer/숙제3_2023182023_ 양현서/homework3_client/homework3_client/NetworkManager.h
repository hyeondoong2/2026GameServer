#ifndef NETWORK_MANAGER_H_
#define NETWORK_MANAGER_H_

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>

#include "stdafx.h"
#include "Common.h"

class NetworkManager {
public:
  static NetworkManager& Instance();

  bool Init();
  void CleanUp();

  SOCKET socket() const;

private:
  NetworkManager() = default;
  ~NetworkManager() = default;

  friend std::default_delete<NetworkManager>;

  // 복사 및 이동 방지 - 싱글톤 원칙
  NetworkManager(const NetworkManager&) = delete;
  NetworkManager& operator=(const NetworkManager&) = delete;
  NetworkManager(NetworkManager&&) = delete;
  NetworkManager& operator=(NetworkManager&&) = delete;

  static std::unique_ptr<NetworkManager> instance_;

  SOCKET socket_ = INVALID_SOCKET;
  CRITICAL_SECTION cs_;

  KeyPacket pending_packet_ = {};
  bool has_pending_ = false;

  bool is_running_ = false;
  void err_disp(const char* msg, int err_no);
};

#endif  // NETWORK_MANAGER_H_