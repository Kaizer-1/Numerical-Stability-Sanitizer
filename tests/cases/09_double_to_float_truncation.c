#include <stdio.h>

static volatile double precise = 100000001.0;
static volatile float big = 100000000.0f;

int main(void) {
  float rounded = (float)precise;
  float y = rounded - big;
  printf("%f\n", y);
  return 0;
}

