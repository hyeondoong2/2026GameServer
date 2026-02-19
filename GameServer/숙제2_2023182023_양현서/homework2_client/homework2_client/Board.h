#ifndef BOARD_H_  
#define BOARD_H_

#include "stdafx.h"

class Board {
public:
  static Board& Instance();

  void Init();
  void Render(HDC hdc);

private:
  Board() = default;
  ~Board() = default;

  friend std::default_delete<Board>;

  // 복사 및 이동 방지
  Board(const Board&) = delete;
  Board& operator=(const Board&) = delete;
  Board(Board&&) = delete;
  Board& operator=(Board&&) = delete;

  static std::unique_ptr<Board> instance_;

  CImage board_image_;
};

#endif  // BOARD_H_