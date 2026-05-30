#include <stdio.h>

int main(void) {
  float big;
  float tiny;

  printf("Enter big and tiny, for example: 100000000 1\n");
  printf("> ");
  fflush(stdout);

  if (scanf("%f %f", &big, &tiny) != 2) {
    fprintf(stderr, "Expected two float values.\n");
    return 1;
  }

  float result = (big + tiny) - big;
  printf("((%.9g + %.9g) - %.9g) = %.9g\n", big, tiny, big, result);
  return 0;
}
