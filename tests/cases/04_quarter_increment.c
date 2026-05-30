#include <stdio.h>

static volatile float big = 16777216.0f;
static volatile float tiny = 0.25f;

int main(void) {
  float y = (big + tiny) - big;
  printf("%f\n", y);
  return 0;
}

