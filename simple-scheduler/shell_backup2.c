#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler.h"
static void *run_scheduler(void *arg)
{
    (void)arg;
    scheduler_loop();
    return NULL;
}
int main(int argc, char **argv)
{
    int NCPU;
    int TSLICE;

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s NCPU TSLICE\n", argv[0]);
        return 1;
    }

    NCPU = atoi(argv[1]);
    TSLICE = atoi(argv[2]);

    if (NCPU <= 0 || TSLICE <= 0)
    {
        fprintf(stderr, "NCPU and TSLICE must be positive integers.\n");
        return 1;
    }

    if (initialize_scheduler(NCPU, TSLICE) != 0)
    {
        return 1;
    }

    printf("Scheduler initialized: NCPU=%d, TSLICE=%d ms\n",
           NCPU, TSLICE);
    pthread_t scheduler_thread;

    if (pthread_create(&scheduler_thread, NULL, run_scheduler, NULL) != 0)
{
    fprintf(stderr, "Failed to start scheduler thread.\n");
    shutdown_scheduler();
    return 1;
}
        char command[512];

    while (1)
    {
        printf("SimpleScheduler> ");
        fflush(stdout);

        if (fgets(command, sizeof(command), stdin) == NULL)
        {
            break;
        }

        command[strcspn(command, "\n")] = '\0';

        if (strcmp(command, "exit") == 0)
        {
            break;
        }
        else if (strncmp(command, "submit ", 7) == 0)
        {
            char *program = command + 7;

            while (*program == ' ')
            {
                program++;
            }

            if (*program == '\0')
            {
                printf("Usage: submit <program_path>\n");
            }
            else if (submit_job(program) != 0)
            {
                printf("Job submission failed.\n");
            }
        }
        else
        {
            printf("Unknown command. Use: submit <program_path> or exit\n");
        }
    }
    request_scheduler_stop();
    pthread_join(scheduler_thread, NULL);
    shutdown_scheduler();
    return 0;
}
