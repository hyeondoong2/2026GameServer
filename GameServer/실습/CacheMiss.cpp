#include <iostream>
#include <chrono>
#include <thread>

#define CACHE_LINE_SIZE 64

using namespace std;
using namespace chrono;


// 메모리를 1KB 부터 2배씩 뻥튀기 해서
// L1->L2->L3->RAM 상태 관찰
// RAM으로 갈수록 느려지는 것을 볼 수 있음
// 따라서 계산은 최대한 캐시에서 처리해야한다

int main() {
  for (int i = 0; i < 20; ++i) {
    const int size = 1024 << i; // 1024에 2를 i번 곱하라는 뜻. 사이즈를 점점 늘림
    char* a = (char*)malloc(size);
    unsigned int index = 0;
    int tmp = 0;
    auto start = high_resolution_clock::now();

    for (int j = 0; j < 100000000; ++j) {
      tmp += a[index % size]; // 나머지 연산으로 크기를 항상 0 ~ size -1 로 고정한다
      index += CACHE_LINE_SIZE * 11;  // 캐시는 한번 읽을 때 64 바이트 덩어리를 가져옴
      // 캐시가 없을 때 얼마나 느린지 보기위해 위 값을 더해주는겨
    }

    auto dur = high_resolution_clock::now() - start;
    cout << "Size : " << size / 1024 << "K,  ";
    cout << "Time " << duration_cast<milliseconds>(dur).count();
    cout << " msec  " << tmp << endl;
  }
}