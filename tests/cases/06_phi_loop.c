#include <stdio.h>

static volatile float start = 100000000.0f;
static volatile float tiny = 1.0f;

int main(void) {
  float acc = start;
  for (int i = 0; i < 4; ++i)
    acc = acc + tiny;
  float y = acc - start;
  printf("%f\n", y);
  return 0;
}

