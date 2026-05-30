#include <stdio.h>

typedef float v4f __attribute__((vector_size(16)));

static volatile float big = 100000000.0f;
static volatile float tiny = 1.0f;

int main(void) {
  v4f a = {big, big, big, big};
  v4f b = {tiny, tiny, tiny, tiny};
  v4f hidden = a + b;
  v4f y = hidden - a;
  printf("%f\n", y[0]);
  return 0;
}

