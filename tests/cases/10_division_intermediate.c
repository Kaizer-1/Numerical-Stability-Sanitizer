#include <stdio.h>

static volatile float big = 100000000.0f;
static volatile float divisor = 4.0f;
static volatile float tiny = 0.25f;

int main(void) {
  float x = big / divisor;
  float y = (x + tiny) - x;
  printf("%f\n", y);
  return 0;
}

