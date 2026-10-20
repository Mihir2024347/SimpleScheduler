#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("Test job 2 started\n");
    fflush(stdout);
    sleep(4);
    printf("Test job 2 finished\n");
    return 0;
}
