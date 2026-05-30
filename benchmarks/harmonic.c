#include <stdio.h>

#ifndef N
#define N 3000000
#endif

int main(void) {
  float sum = 0.0f;
  for (int i = 1; i <= N; ++i)
    sum += 1.0f / (float)i;
  printf("%f\n", sum);
  return 0;
}

