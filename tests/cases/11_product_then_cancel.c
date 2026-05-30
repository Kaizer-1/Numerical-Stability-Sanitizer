#include <stdio.h>

static volatile float a = 4096.0f;
static volatile float b = 4096.0f;
static volatile float tiny = 1.0f;

int main(void) {
  float x = a * b;
  float y = (x + tiny) - x;
  printf("%f\n", y);
  return 0;
}

