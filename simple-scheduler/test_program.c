#include <stdio.h>
#include <unistd.h>
#include <time.h>

int main(void)
{
    for (int i = 1; i <= 20; i++)
    {
        printf("PID %ld: iteration %d\n", (long)getpid(), i);
        fflush(stdout);

        struct timespec delay;
        delay.tv_sec = 0;
        delay.tv_nsec = 200000000L;

        nanosleep(&delay, NULL);
    }

    return 0;
}
