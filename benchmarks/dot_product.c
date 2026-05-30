#include <stdio.h>

#ifndef N
#define N 2000000
#endif

int main(void) {
  float acc = 0.0f;
  for (int i = 1; i <= N; ++i) {
    float a = (float)(i % 97) * 0.125f;
    float b = (float)(i % 31) * 0.25f;
    acc += a * b;
  }
  printf("%f\n", acc);
  return 0;
}

