#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "err.h"
#include "utils.h"

#define MAX_N_TASKS (4096)
#define MAX_TASK_LENGTH (512)
#define MAX_LINE (1022)
#define MAX_END_OUT (50)

#define debug (0)

struct Task {
    _Atomic int id;
    _Atomic pid_t pid;
    pthread_t thread;
    char out[MAX_LINE];
    pthread_mutex_t outSem;
    char err[MAX_LINE];
    pthread_mutex_t errSem;
    char** argv;
};

struct lineInfo {
    pthread_mutex_t* mutex;
    char* buf;
    int descriptor;
};

struct Task tasks[MAX_N_TASKS];
bool busy = false;
pthread_mutex_t busyQueue;
sem_t handledTask;
char finished[MAX_N_TASKS * MAX_END_OUT];
int idxFinished = 0;

void acually_working_split_string(char** splitLine, char* line)
{
    int i = 0;
    splitLine[i] = strtok(line, " ");
    while (splitLine[i] != NULL) {
        i++;
        splitLine[i] = strtok(NULL, " ");
    }
}

void* readLines(void* lineInfo)
{
    struct lineInfo* info = (struct lineInfo*)lineInfo;
    char line[MAX_LINE];
    FILE* fileToRead;
    if (debug)
        fprintf(stderr, "OUT/ERR zaczyna\n");
    if ((fileToRead = fdopen(info->descriptor, "r")) == NULL) {
        printf("ERROR!!!! COULD NOT OPEN A FILE\n");
        return NULL;
    }
    setbuf(fileToRead, 0);
    while (true) {
        if (debug)
            printf("OUT/ERR czyta linie\n");
        if (fgets(line, MAX_LINE, fileToRead) == NULL) {
            break;
        }
        strtok(line, "\r\n");
        if (debug)
            printf("PRZECZYTAL: %s\n", line);
        if (debug)
            printf("OUT/ERR probuje ja zapisac\n");
        pthread_mutex_lock(info->mutex);
        // int i = 0;
        // for (int i = 0; line[i] != '\0'; i++) {
        //     info->buf[i] = line[i];
        // }
        // info->buf[i] = '\0';
        // sprintf(info->buf, "%s", line);
        strcpy(info->buf, line);
        pthread_mutex_unlock(info->mutex);
    }

    if (debug)
        printf("OUT/ERR konczy...\n");
    fclose(fileToRead);
    if (debug)
        printf("OUT/ERR skonczyl\n");
    return NULL;
}

void finishInfo(int taskNo, int exitStatus)
{
    char info[MAX_END_OUT];
    if (WIFEXITED(exitStatus)) {
        sprintf(info, "Task %d ended: status %d.\n", taskNo, WEXITSTATUS(exitStatus));
    } else {
        sprintf(info, "Task %d ended: signalled.\n", taskNo);
    }
    ASSERT_ZERO(pthread_mutex_lock(&busyQueue));
    if (busy) {
        char* q = finished + idxFinished;
        strcpy(q, info);
        idxFinished += strlen(info);
    } else {
        printf("%s", info);
    }
    ASSERT_ZERO(pthread_mutex_unlock(&busyQueue));
}

void* runTask(void* taskPtr)
{
    struct Task* task = (struct Task*)taskPtr;
    int fdOut[2];
    ASSERT_SYS_OK(pipe(fdOut));
    int fdErr[2];
    ASSERT_SYS_OK(pipe(fdErr));
    pid_t pid = fork();
    ASSERT_SYS_OK(pid);
    if (!pid) {
        if (debug)
            printf("Bachor stworzony\n");
        ASSERT_SYS_OK(close(fdOut[0]));
        ASSERT_SYS_OK(close(fdErr[0]));
        if (debug)
            printf("Bachor odjebal pipe\n");
        ASSERT_SYS_OK(dup2(fdOut[1], STDOUT_FILENO));
        ASSERT_SYS_OK(dup2(fdErr[1], STDERR_FILENO));
        ASSERT_SYS_OK(close(fdOut[1]));
        ASSERT_SYS_OK(close(fdErr[1]));
        // printf("Program: %s\n", task->argv[1]);
        // if (strcmp(task->argv[1], "./01_out")) {
        //     for (int i = 0; task->argv[1][i] != '\0'; i++) {
        //         printf("%d ", task->argv[1][i]);
        //     }
        //     printf("BAJO JAJO\n");
        // }
        execvp(task->argv[1], task->argv + 1);
        // execvp("./01_out", task->argv + 1);
    } else {
        printf("Task %d started: pid %d.\n", task->id, pid);
        if (debug)
            printf("Stary rozpoczyna\n");
        close(fdOut[1]);
        close(fdErr[1]);
        task->pid = pid;
        pthread_t out;
        pthread_t err;
        void* outExitStatus;
        void* errExitStatus;
        struct lineInfo outInfo;
        struct lineInfo errInfo;
        outInfo.buf = task->out;
        outInfo.descriptor = fdOut[0];
        outInfo.mutex = &task->outSem;
        errInfo.buf = task->err;
        errInfo.descriptor = fdErr[0];
        errInfo.mutex = &task->errSem;
        int pidStatus;
        pthread_attr_t attr;
        if (debug)
            printf("Stary tworzy watki\n");
        ASSERT_ZERO(pthread_attr_init(&attr));
        ASSERT_ZERO(pthread_create(&out, &attr, readLines, &outInfo));
        ASSERT_ZERO(pthread_create(&err, &attr, readLines, &errInfo));
        if (debug)
            printf("Stary stworzyl watki\n");
        ASSERT_SYS_OK(sem_post(&handledTask));
        ASSERT_SYS_OK(waitpid(pid, &pidStatus, 0));
        if (debug)
            printf("STARY CO TY ODPIERDALASZ\n");
        ASSERT_ZERO(pthread_join(out, &outExitStatus));
        ASSERT_ZERO(pthread_join(err, &errExitStatus));
        ASSERT_ZERO(pthread_attr_destroy(&attr));
        finishInfo(task->id, pidStatus);
        free_split_string(task->argv);
    }
    return NULL;
}

int main()
{
    setbuf(stdout, 0);
    setbuf(stdin, 0);
    char line[MAX_LINE];
    char** splitLine;
    int tasksCount = 0;
    pthread_attr_t attr;
    ASSERT_ZERO(pthread_attr_init(&attr));
    ASSERT_ZERO(pthread_mutex_init(&busyQueue, NULL));
    ASSERT_SYS_OK(sem_init(&handledTask, 0, 0));
    while (fgets(line, MAX_LINE, stdin)) {
        ASSERT_ZERO(pthread_mutex_lock(&busyQueue));
        busy = true;
        ASSERT_ZERO(pthread_mutex_unlock(&busyQueue));
        if (!strcmp(line, "\r\n") || !strcmp(line, "\n"))
            continue;
        strtok(line, "\r\n");
        if (debug)
            printf("MAM WYJEBANE\n");
        if (debug)
            printf("chuj%d\n", 0);
        splitLine = split_string(line);
        if (!strcmp(splitLine[0], "run")) {
            tasks[tasksCount].id = tasksCount;
            if (debug)
                printf("chuj2\n");
            ASSERT_ZERO(pthread_mutex_init(&(tasks[tasksCount].outSem), NULL));
            ASSERT_ZERO(pthread_mutex_init(&(tasks[tasksCount].errSem), NULL));
            if (debug)
                printf("chuj2.5\n");
            tasks[tasksCount].argv = splitLine;
            if (debug)
                printf("chuj3\n");
            ASSERT_ZERO(pthread_create(&(tasks[tasksCount].thread), &attr,
                runTask, &tasks[tasksCount]));
            if (debug)
                printf("chuj4\n");
            tasksCount++;
            ASSERT_SYS_OK(sem_wait(&handledTask));
        } else {
            if (!strcmp(splitLine[0], "out")) {
                int taskId = atoi(splitLine[1]);
                pthread_mutex_lock(&(tasks[taskId].outSem));
                printf("Task %d stdout: \'%s\'.\n", taskId, tasks[taskId].out);
                pthread_mutex_unlock((&(tasks[taskId].outSem)));
            } else if (!strcmp(splitLine[0], "err")) {
                int taskId = atoi(splitLine[1]);
                pthread_mutex_lock(&(tasks[taskId].errSem));
                printf("Task %d stderr: \'%s\'.\n", taskId, tasks[taskId].err);
                pthread_mutex_unlock((&(tasks[taskId].errSem)));
            } else if (!strcmp(splitLine[0], "kill")) {
                int taskId = atoi(splitLine[1]);
                kill(tasks[taskId].pid, SIGINT);
            } else if (!strcmp(splitLine[0], "sleep")) {
                int miliseconds = atoi(splitLine[1]);
                usleep(miliseconds * 1000);
            } else if (!strcmp(splitLine[0], "quit")) { // quit
                free_split_string(splitLine);
                break;
            }
            free_split_string(splitLine);
            // else if (!strcmp(splitLine[0], "sleep")) {
            //     int seconds = atoi(splitLine[1]);
            //     ASSERT_SYS_OK(sleep(seconds));
            // }
            // else if (!strcmp(splitLine[0], "quit")) {
            // quit
            // }
        }
        // if (strcmp(splitLine[0], "run")) {
        //     free(line);
        // }
        ASSERT_ZERO(pthread_mutex_lock(&busyQueue));
        busy = false;
        if (idxFinished > 0) {
            // printf("PRINTING QUEUE\n");
            printf("%s", finished);
            idxFinished = 0;
            finished[0] = '\0';
        }
        ASSERT_ZERO(pthread_mutex_unlock(&busyQueue));
    }
    ASSERT_ZERO(pthread_mutex_lock(&busyQueue));
    busy = false;
    if (idxFinished > 0) {
        // printf("PRINTING QUEUE\n");
        printf("%s", finished);
        idxFinished = 0;
        finished[0] = '\0';
    }
    ASSERT_ZERO(pthread_mutex_unlock(&busyQueue));
    for (int i = 0; i < tasksCount; i++) {
        kill(tasks[i].pid, SIGINT);
        pthread_join(tasks[i].thread, NULL);
        pthread_mutex_destroy(&tasks[i].errSem);
        pthread_mutex_destroy(&tasks[i].outSem);
    }
    ASSERT_ZERO(pthread_mutex_destroy(&busyQueue));
    ASSERT_ZERO(pthread_attr_destroy(&attr));
    ASSERT_SYS_OK(sem_destroy(&handledTask));

    return 0;
}
