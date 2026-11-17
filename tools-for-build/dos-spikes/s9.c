#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) { printf("returning %d\n", argc > 1 ? atoi(argv[1]) : 5); if (argc > 2) exit(atoi(argv[1])); return argc > 1 ? atoi(argv[1]) : 5; }
