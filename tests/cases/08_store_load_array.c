#include <stdio.h>

static volatile float big = 100000000.0f;
static volatile float tiny = 5.0f;

int main(void) {
  float values[2];
  values[0] = big + tiny;
  values[1] = big;
  float y = values[0] - values[1];
  printf("%f\n", y);
  return 0;
}

