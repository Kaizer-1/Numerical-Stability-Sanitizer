#include <stdio.h>

static volatile float big = 100000000.0f;
static volatile float tiny = 1.0f;

int main(void) {
  float saved = big + tiny;
  float y = saved - big;
  printf("%f\n", y);
  return 0;
}

