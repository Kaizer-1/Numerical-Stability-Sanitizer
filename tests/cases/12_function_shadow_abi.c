#include <stdio.h>

static volatile float big = 100000000.0f;
static volatile float tiny = 1.0f;

__attribute__((noinline)) static float subtract(float x, float y) {
  return x - y;
}

int main(void) {
  float hidden = big + tiny;
  float y = subtract(hidden, big);
  printf("%f\n", y);
  return 0;
}

