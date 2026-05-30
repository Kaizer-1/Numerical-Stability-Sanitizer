// NSAN_EXPECT: no-diagnostic
#include <stdint.h>
#include <stdio.h>

static volatile float big = 100000000.0f;
static volatile float tiny = 1.0f;

int main(void) {
  union {
    float f;
    uint32_t u;
  } value;

  value.f = big + tiny;
  value.u = 0u;

  float y = value.f + big;
  printf("%f\n", y);
  return 0;
}
