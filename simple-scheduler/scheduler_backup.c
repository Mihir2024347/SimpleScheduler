#include <pthread.h>
#include <stdbool.h>
#include "scheduler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>

static ReadyQueue ready_queue;

static pthread_mutex_t scheduler_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t job_available = PTHREAD_COND_INITIALIZER;
static bool scheduler_stopping = false;

static int number_of_cpus = 1;
static int time_slice = 100;
static int scheduler_initialized = 0;

static Job all_jobs[MAX_JOBS];
static int total_jobs = 0;

static double queued_at[MAX_JOBS];
static double total_wait_seconds[MAX_JOBS];

static double current_time_seconds(void)
{
    struct timespec current_time;

    if (clock_gettime(CLOCK_MONOTONIC, &current_time) == -1) {
        perror("clock_gettime");
        return 0.0;
    }

    return (double) current_time.tv_sec +
           (double) current_time.tv_nsec / 1000000000.0;
}

static void ready_queue_initializing(ReadyQueue *ready_queue)
{
    ready_queue->front = 0;
    ready_queue->rear = 0;
    ready_queue->job_count = 0;
}

static int ready_queue_is_empty(const ReadyQueue *ready_queue)
{
    return ready_queue->job_count == 0;
}

static int ready_queue_is_full(const ReadyQueue *ready_queue)
{
    return ready_queue->job_count == MAX_JOBS;
}

static int enqueue_job(ReadyQueue *ready_queue, const Job *job)
{
    if (ready_queue_is_full(ready_queue)) {
        fprintf(stderr, "Error: ready queue is full.\n");
        return -1;
    }

    ready_queue->jobs[ready_queue->rear] = *job;

    ready_queue->rear =
        (ready_queue->rear + 1) % MAX_JOBS;

    ready_queue->job_count++;

    return 0;
}

static int dequeue_job(ReadyQueue *ready_queue, Job *job)
{
    if (ready_queue_is_empty(ready_queue)) {
        return -1;
    }

    *job = ready_queue->jobs[ready_queue->front];

    ready_queue->front =
        (ready_queue->front + 1) % MAX_JOBS;

    ready_queue->job_count--;

    return 0;
}

static int find_job_index(pid_t pid)
{
    int i;

    for (i = 0; i < total_jobs; i++) {
        if (all_jobs[i].pid == pid) {
            return i;
        }
    }

    return -1;
}

static void record_waiting_time(int index)
{
    double current_time;
    double waiting_time;

    current_time = current_time_seconds();

    waiting_time = current_time - queued_at[index];

    if (waiting_time > 0.0) {
        total_wait_seconds[index] += waiting_time;
    }

    all_jobs[index].total_wait_time =
        (int) total_wait_seconds[index];
}

int initialize_scheduler(int NCPU, int TSLICE)
{
    if (NCPU <= 0 || TSLICE <= 0) {
        fprintf(stderr,
                "Error: NCPU and TSLICE must be positive.\n");
        return -1;
    }

    number_of_cpus = NCPU;
    time_slice = TSLICE;

    total_jobs = 0;

    memset(all_jobs, 0, sizeof(all_jobs));
    memset(queued_at, 0, sizeof(queued_at));
    memset(total_wait_seconds, 0,
           sizeof(total_wait_seconds));

    ready_queue_initializing(&ready_queue);

    pthread_mutex_lock(&scheduler_mutex);
    scheduler_stopping = false;
    pthread_mutex_unlock(&scheduler_mutex);
    scheduler_initialized = 1;

    return 0;
}

int submit_job(const char *program_name)
{
    pid_t pid;
    Job new_job;
    int status;
    int index;

    if (!scheduler_initialized) {
        fprintf(stderr,
                "Error: initialize_scheduler must be called first.\n");
        return -1;
    }

    if (program_name == NULL || program_name[0] == '\0') {
        fprintf(stderr, "Error: invalid program name.\n");
        return -1;
    }

    if (total_jobs >= MAX_JOBS) {
        fprintf(stderr, "Error: maximum job limit reached.\n");
        return -1;
    }

    if (ready_queue_is_full(&ready_queue)) {
        fprintf(stderr, "Error: ready queue is full.\n");
        return -1;
    }

    memset(&new_job, 0, sizeof(new_job));

    strncpy(new_job.program_name,
            program_name,
            MAX_PROGRAM_NAME - 1);

    new_job.program_name[MAX_PROGRAM_NAME - 1] = '\0';

    new_job.submission_time = time(NULL);
    new_job.completion_time = 0;
    new_job.completed = 0;
    new_job.total_wait_time = 0;

    pid = fork();

    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        char *arguments[2];

        arguments[0] = (char *) program_name;
        arguments[1] = NULL;
        if (raise(SIGSTOP) != 0) {
            perror("raise");
            _exit(1);
        }

        execvp(program_name, arguments);

        perror("execvp");
        _exit(127);
        }

    while (waitpid(pid, &status, WUNTRACED) == -1) {
        if (errno != EINTR) {
            perror("waitpid");
            kill(pid, SIGKILL);
            waitpid(pid, NULL, 0);
            return -1;
        }
    }

    if (!WIFSTOPPED(status)) {
        fprintf(stderr, "Error: child process did not stop.\n");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }

    new_job.pid = pid;

    index = total_jobs;

    all_jobs[index] = new_job;

    queued_at[index] = current_time_seconds();
    total_wait_seconds[index] = 0.0;

    if (enqueue_job(&ready_queue, &new_job) == -1) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        memset(&all_jobs[index], 0, sizeof(Job));
        return -1;
    }

    total_jobs++;
    pthread_cond_signal(&job_available);

    return 0;
}

void scheduler_loop(void)
{
    Job running_jobs[MAX_JOBS];
    int running_count;
    int i;
    int status;
    int remaining_jobs;

    if (!scheduler_initialized) {
        fprintf(stderr,
                "Error: scheduler has not been initialized.\n");
        return;
    }

    while (1) {
    pthread_mutex_lock(&scheduler_mutex);
    int should_stop = scheduler_stopping;
    pthread_mutex_unlock(&scheduler_mutex);

    if (should_stop && ready_queue_is_empty(&ready_queue)) {
    break;
    }
    struct timespec slice_duration;

    running_count = 0; {
        struct timespec slice_duration;

        running_count = 0;
        while (running_count < number_of_cpus &&
               !ready_queue_is_empty(&ready_queue)) {
            Job current_job;

            if (dequeue_job(&ready_queue, &current_job) == -1) {
                break;
            }

            running_jobs[running_count] = current_job;
            running_count++;
        }
        for (i = 0; i < running_count; i++) {
            int index;

            index = find_job_index(running_jobs[i].pid);

            if (index >= 0) {
                record_waiting_time(index);
            }

            if (kill(running_jobs[i].pid, SIGCONT) == -1) {
                if (errno != ESRCH) {
                    perror("SIGCONT");
                }
            }
        }
        slice_duration.tv_sec = time_slice / 1000;

        slice_duration.tv_nsec =
            (long) (time_slice % 1000) * 1000000L;

        while (nanosleep(&slice_duration, &slice_duration) == -1) {
            if (errno != EINTR) {
                perror("nanosleep");
                break;
            }
        }
        for (i = 0; i < running_count; i++) {
            pid_t pid;
            pid_t result;
            int index;

            pid = running_jobs[i].pid;

            index = find_job_index(pid);

            result = waitpid(pid, &status, WNOHANG);

            if (result == pid) {
                if (index >= 0) {
                    all_jobs[index].completed = 1;
                    all_jobs[index].completion_time = time(NULL);
                }

                continue;
            }

            if (result == -1) {
                if (errno == EINTR) {
                    i--;
                    continue;
                }

                if (errno == ECHILD) {
                    if (index >= 0) {
                        all_jobs[index].completed = 1;
                        all_jobs[index].completion_time = time(NULL);
                    }

                    continue;
                }

                perror("waitpid");
                continue;
            }
            if (kill(pid, SIGSTOP) == -1) {
                if (errno != ESRCH) {
                    perror("SIGSTOP");
                }
            }
            while (waitpid(pid, &status, WUNTRACED) == -1) {
                if (errno == EINTR) {
                    continue;
                }

                if (errno == ECHILD) {
                    break;
                }

                perror("waitpid");
                break;
            }
            if (WIFEXITED(status) || WIFSIGNALED(status)) {
                if (index >= 0) {
                    all_jobs[index].completed = 1;
                    all_jobs[index].completion_time = time(NULL);
                }

                continue;
            }
            if (index >= 0) {
                queued_at[index] = current_time_seconds();
            }

            if (enqueue_job(&ready_queue,
                            &running_jobs[i]) == -1) {
                fprintf(stderr,
                        "Error: could not requeue process %ld.\n",
                        (long) pid);

                kill(pid, SIGKILL);
                waitpid(pid, NULL, 0);

                if (index >= 0) {
                    all_jobs[index].completed = 1;
                    all_jobs[index].completion_time = time(NULL);
                }
            }
        }

        remaining_jobs = ready_queue.job_count;

        (void) remaining_jobs;
    }
}

void shutdown_scheduler(void)
{
    int i;

    if (!scheduler_initialized) {
        return;
    }
    pthread_mutex_lock(&scheduler_mutex);
    scheduler_stopping = true;
    pthread_cond_broadcast(&job_available);
    pthread_mutex_unlock(&scheduler_mutex);

    printf("\n");
    printf("========== Scheduler Report ==========\n");
    printf("%-24s %-10s %-20s %-15s\n",
           "Program", "PID", "Completion Time", "Wait Time");

    for (i = 0; i < total_jobs; i++) {
        char completion_string[64];

        if (all_jobs[i].completion_time != 0) {
            struct tm completion_tm;

            if (localtime_r(&all_jobs[i].completion_time,
                            &completion_tm) != NULL) {
                strftime(completion_string,
                         sizeof(completion_string),
                         "%Y-%m-%d %H:%M:%S",
                         &completion_tm);
            } else {
                strcpy(completion_string, "Unknown");
            }
        } else {
            strcpy(completion_string, "Not completed");
        }

        printf("%-24s %-10ld %-20s %-15d\n",
               all_jobs[i].program_name,
               (long) all_jobs[i].pid,
               completion_string,
               all_jobs[i].total_wait_time);
    }

    printf("======================================\n");

    scheduler_initialized = 0;
}
void request_scheduler_stop(void)
{
    pthread_mutex_lock(&scheduler_mutex);

    scheduler_stopping = true;
    pthread_cond_broadcast(&job_available);

    pthread_mutex_unlock(&scheduler_mutex);
}
