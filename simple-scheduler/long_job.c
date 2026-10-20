#include <stdio.h>
#include "dummy_main.h"

int main(int argc, char **argv)
{
    volatile unsigned long i;
    volatile unsigned long count;

    (void)argc;
    (void)argv;

    count = 0;

    for (i = 0; i < 5000000000UL; i++) {
        count++;
    }

    printf("Long job finished: %lu\n", count);

    return 0;
}
