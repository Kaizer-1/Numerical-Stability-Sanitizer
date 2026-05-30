// NSAN_EXPECT: no-diagnostic
#include <stdio.h>
#include <string.h>

static volatile float big = 100000000.0f;
static volatile float tiny = 1.0f;

int main(void) {
  float x = big + tiny;
  memset(&x, 0, sizeof(float));
  float y = x + big;
  printf("%f\n", y);
  return 0;
}
