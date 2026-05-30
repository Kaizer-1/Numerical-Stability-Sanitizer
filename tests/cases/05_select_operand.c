#include <stdio.h>

static volatile int choose = 1;
static volatile float pos = 100000000.0f;
static volatile float neg = -100000000.0f;
static volatile float tiny = 3.0f;

int main(void) {
  float x = choose ? pos : neg;
  float y = (x + tiny) - x;
  printf("%f\n", y);
  return 0;
}

