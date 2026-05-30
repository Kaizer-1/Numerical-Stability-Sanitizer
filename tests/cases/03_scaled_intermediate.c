#include <stdio.h>

static volatile float base = 100000.0f;
static volatile float tiny = 1.0f;

int main(void) {
  float x = base * base;
  float y = (x + tiny) - x;
  printf("%f\n", y);
  return 0;
}

