// NSAN_EXPECT: detect
#include <stdio.h>
#include <string.h>

static volatile float big = 100000000.0f;
static volatile float tiny = 1.0f;

int main(void) {
  float values[2];
  values[0] = big + tiny;
  values[1] = 0.0f;
  memmove(&values[1], &values[0], sizeof(float));
  float y = values[1] - big;
  printf("%f\n", y);
  return 0;
}

