#include <iostream>
#include <chrono>
#include <thread>

using namespace std;
using namespace chrono;

int main() {
  volatile long long tmp = 0;
  auto start = high_resolution_clock::now();

  for (int j = 0;j <= 10000000;++j) {
    tmp += j;

    // 시스템콜
    // 주석하고 하는거랑 아닌거랑 차이마니남
    // 시스템콜은 비싸니까 남발하지 말자
    this_thread::yield();
  }

  auto duration = high_resolution_clock::now() - start;
  cout << "Time " << duration_cast<milliseconds>(duration).count();
  cout << " msec\n";
  cout << "RESULT " << tmp << endl;
}
