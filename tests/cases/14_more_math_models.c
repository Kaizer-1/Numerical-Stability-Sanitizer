#include <math.h>
#include <stdio.h>

static volatile float big = 100000000.0f;
static volatile float tiny = 1.0f;

int main(void) {
  float hidden = big + tiny;
  float folded = fabsf(hidden);
  float y = folded - big;
  printf("%f\n", y);
  return 0;
}

