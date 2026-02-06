#ifndef GAME_FRAMEWORK_H_  
#define GAME_FRAMEWORK_H_

#include "stdafx.h"

class GameFramework {
public:
  // static : 프로그램에서 하나만 존재
  // 프레임웤은 하나니까 싱글톤으로 만든다.
  static GameFramework& Instance();

  void Init();
  void Progress();
  void CleanUp();

  void OnWindowMessage(HWND hwnd,
    UINT message_id,
    WPARAM w_param,
    LPARAM l_param);

  void set_hinstance(HINSTANCE hinstance);
  HINSTANCE hinstance() const;

  void set_hwnd(HWND hwnd);

private:
  GameFramework() = default;
  ~GameFramework() = default;

  friend std::default_delete<GameFramework>;

  // 복사 및 이동 방지 - 싱글톤 원칙
  GameFramework(const GameFramework&) = delete;
  GameFramework& operator=(const GameFramework&) = delete;
  GameFramework(GameFramework&&) = delete;
  GameFramework& operator=(GameFramework&&) = delete;

  static std::unique_ptr<GameFramework> instance_;

  HINSTANCE hinstance_ = nullptr;
  HWND hwnd_ = nullptr;
  HDC hdc_ = nullptr;
  bool draw_enabled_ = false;
};

#endif  // GAME_FRAMEWORK_H_