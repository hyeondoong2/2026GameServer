#include "Board.h"

std::unique_ptr<Board> Board::instance_ = nullptr;

Board& Board::Instance() {
  if (instance_ == nullptr) {
    instance_.reset(new Board());
  }
  return *instance_;
}

void Board::Init() {
  HRESULT result = board_image_.Load(L"board.png");

  if (FAILED(result)) {
    OutputDebugString(L"Board Image Load Failed!\n");
  }
}

void Board::Render(HDC hdc) {
  if (!board_image_.IsNull()) {
    board_image_.Draw(hdc, 0, 0);
  }
}
