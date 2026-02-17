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

  void SendKeyPacket(const KeyPacket& packet);

private:
  NetworkManager() = default;
  ~NetworkManager() = default;

  friend std::default_delete<NetworkManager>;

  // 복사 및 이동 방지 - 싱글톤 원칙
  NetworkManager(const NetworkManager&) = delete;
  NetworkManager& operator=(const NetworkManager&) = delete;
  NetworkManager(NetworkManager&&) = delete;
  NetworkManager& operator=(NetworkManager&&) = delete;

  static DWORD WINAPI RecvWorker(LPVOID lpParam);
  static DWORD WINAPI SendWorker(LPVOID lpParam);

  static std::unique_ptr<NetworkManager> instance_;

  SOCKET socket_ = INVALID_SOCKET;
  HANDLE recv_thread_handle_ = nullptr;
  HANDLE send_thread_handle_ = nullptr;
  CRITICAL_SECTION cs_;

  KeyPacket pending_packet_ = {};
  bool has_pending_ = false;

  bool is_running_ = false;
  void err_disp(const char* msg, int err_no);
};

#endif  // NETWORK_MANAGER_H_