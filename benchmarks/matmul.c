#include <stdio.h>

#ifndef N
#define N 72
#endif

static float a[N][N];
static float b[N][N];
static float c[N][N];

int main(void) {
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      a[i][j] = (float)((i + j) % 19) * 0.5f;
      b[i][j] = (float)((i * 3 + j) % 23) * 0.25f;
    }
  }
  for (int i = 0; i < N; ++i)
    for (int k = 0; k < N; ++k)
      for (int j = 0; j < N; ++j)
        c[i][j] += a[i][k] * b[k][j];
  printf("%f\n", c[N - 1][N - 1]);
  return 0;
}

