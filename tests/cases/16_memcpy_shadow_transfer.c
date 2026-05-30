// NSAN_EXPECT: detect
#include <stdio.h>
#include <string.h>

static volatile float big = 100000000.0f;
static volatile float tiny = 1.0f;

int main(void) {
  float src = big + tiny;
  float dst = 0.0f;
  memcpy(&dst, &src, sizeof(float));
  float y = dst - big;
  printf("%f\n", y);
  return 0;
}

