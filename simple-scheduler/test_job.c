#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("Test job started\n");
    fflush(stdout);
    sleep(2);
    printf("Test job finished\n");
    return 0;
}
