#include <pthread.h>
#include <semaphore.h>
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

struct Task {
    int id;
    pthread_t thread;
    char out[MAX_LINE];
    pthread_mutex_t outSem;
    char err[MAX_LINE];
    pthread_mutex_t errSem;
    char** argv;
};

struct lineInfo {
    pthread_mutex_t mutex;
    char* buf;
    int descriptor;
};

struct Task tasks[MAX_N_TASKS];
bool busy = false;
// char task[MAX_TASK_LENGTH];
char infoQueue[MAX_N_TASKS * MAX_END_OUT];
int idxInfoQueue = 0;

void acually_working_split_string(char** splitLine, char* line)
{
    int i = 0;
    char separators[3];
    separators[0] = (char)' ';
    separators[1] = (char)10;
    separators[2] = '\0';
    splitLine[i] = strtok(line, separators);
    while (splitLine[i] != NULL) {
        i++;
        splitLine[i] = strtok(NULL, separators);
    }
}

void* readLines(void* lineInfo)
{
    struct lineInfo* info = (struct lineInfo*)lineInfo;
    char line[MAX_LINE];
    FILE* fileToRead;
    fprintf(stderr, "OUT/ERR zaczyna\n");
    if ((fileToRead = fdopen(info->descriptor, "r")) == NULL) {
        printf("ERROR!!!! COULD NOT OPEN A FILE\n");
        return NULL;
    }
    while (true) {
        printf("OUT/ERR czyta linie\n");
        if (fgets(line, MAX_LINE, fileToRead) == NULL) {
            break;
        }
        printf("OUT/ERR probuje ja zapisac\n");
        pthread_mutex_lock(&info->mutex);
        strcpy(info->buf, line);
        pthread_mutex_unlock(&info->mutex);
    }

    printf("OUT/ERR konczy...\n");
    fclose(fileToRead);
    // sleep(10);
    printf("OUT/ERR skonczyl\n");
    return NULL;
}

// char* finishInfo(int taskNo, int exitStatus)
// {
//     char info[MAX_END_OUT];
//     if (WIFEXITED(exitStatus)) {
//         sprintf(info, "Task %d ended: status %d.\n", taskNo, exitStatus);
//     } else {
//         sprintf(info, "Task %d ended: signalled.\n", taskNo);
//     }
//     return info;
// }

void* runTask(void* taskPtr)
{
    struct Task* task = (struct Task*)taskPtr;
    int fdOut[2];
    for (int i = 0; (task->argv)[i] != NULL; ++i)
        printf("task->argv[%d]: %s\n", i, (task->argv)[i]);
    // printf("task->argv[1]: %s\n");
    ASSERT_SYS_OK(pipe(fdOut));
    int fdErr[2];
    ASSERT_SYS_OK(pipe(fdErr));
    pid_t pid = fork();
    ASSERT_SYS_OK(pid);
    if (!pid) {
        // child?
        printf("Bachor stworzony\n");
        ASSERT_SYS_OK(close(fdOut[0]));
        ASSERT_SYS_OK(close(fdErr[0]));
        printf("Bachor odjebal pipe\n");
        ASSERT_SYS_OK(dup2(fdOut[1], STDOUT_FILENO));
        // printf("Bachor odjebal dup2\n");
        ASSERT_SYS_OK(dup2(fdErr[1], STDERR_FILENO));
        // printf("Bachor odjebal dup2\n");
        ASSERT_SYS_OK(close(fdOut[1]));
        ASSERT_SYS_OK(close(fdErr[1]));
        // printf("Bachor URUCHAMIA PROGRAM\n");
        // sleep(100);
        // printf("task->argv[1]: %s\n", tas);
        // printf("LINE LINE LINE %s\n", (task->argv + 1)[0]);
        // sleep(10);
        // if (!strcmp(task->argv[1], "./number")) {
        //     printf("to samo\n");
        // } else {
        //     int i = 0;
        //     while (task->argv[1][i] != '\0') {
        //         printf("%d ", task->argv[1][i++]);
        //     }
        //     printf("co innego\n");
        // }
        // printf("__________________\n");
        execvp((task->argv)[1], task->argv + 1);
        // sleep(1);
        // execvp("./number", task->argv + 1);
    } else {
        printf("Stary rozpoczyna\n");
        close(fdOut[1]);
        close(fdErr[1]);
        pthread_t out;
        pthread_t err;
        void* outExitStatus;
        void* errExitStatus;
        struct lineInfo outInfo;
        struct lineInfo errInfo;
        outInfo.buf = task->out;
        outInfo.descriptor = fdOut[0];
        outInfo.mutex = task->outSem;
        errInfo.buf = task->err;
        errInfo.descriptor = fdErr[0];
        errInfo.mutex = task->errSem;
        int pidStatus;
        pthread_attr_t attr;
        printf("Stary tworzy watki\n");
        ASSERT_ZERO(pthread_attr_init(&attr));
        ASSERT_ZERO(pthread_create(&out, &attr, readLines, &outInfo));
        ASSERT_ZERO(pthread_create(&err, &attr, readLines, &errInfo));
        printf("Stary stworzyl watki\n");
        // create threads to readLines
        ASSERT_SYS_OK(waitpid(pid, &pidStatus, 0));
        printf("STARY CO TY ODPIERDALASZ\n");
        ASSERT_ZERO(pthread_join(out, &outExitStatus));
        ASSERT_ZERO(pthread_join(err, &errExitStatus));
        // mutex
        char info[MAX_END_OUT];
        if (WIFEXITED(*(int*)outExitStatus)) {
            printf(info, "Task %d ended: status %d.\n", task->id, *(int*)outExitStatus);
        } else {
            printf(info, "Task %d ended: signalled.\n", task->id);
        }
        // if (busy) {
        // char* q = infoQueue + idxInfoQueue;
        // char* info = finishInfo(task->id, pidStatus);
        // strcpy(q, info);
        // } else {
        // printf("%s", finishInfo(task->id, pidStatus));
        // } // mutex
    }
    return NULL;
}

int main()
{
    char line[MAX_LINE];
    char* splitLine[MAX_TASK_LENGTH];
    int tasksCount = 0;
    pthread_attr_t attr;
    ASSERT_ZERO(pthread_attr_init(&attr));
    printf("MAM WYJEBANE\n");
    while (fgets(line, MAX_LINE, stdin)) {
        printf("line: %s\n", line);
        int i = 0;
        printf("LINE( ");
        while (line[i] != '\0') {
            printf("%d ", line[i++]);
        }
        printf(")\n");

        busy = true;
        printf("chuj%d\n", 0);
        acually_working_split_string(splitLine, line);
        for (int i = 0; splitLine[i] != NULL; ++i)
            printf("%d: %s\n", i, splitLine[i]);
        if (!strcmp(splitLine[0], "run")) {
            tasks[tasksCount].id = tasksCount;
            printf("chuj2\n");
            ASSERT_ZERO(pthread_mutex_init(&(tasks[tasksCount].outSem), NULL));
            ASSERT_ZERO(pthread_mutex_init(&(tasks[tasksCount].errSem), NULL));
            printf("chuj2.5\n");
            tasks[tasksCount].argv = splitLine;
            printf("chuj3\n");
            ASSERT_ZERO(pthread_create(&(tasks[tasksCount].thread), &attr, runTask,
                &tasks[tasksCount]));
            printf("chuj4\n");
            tasksCount++;
        }

        busy = false;
        if (idxInfoQueue > 0) {
            printf("%s", infoQueue);
            idxInfoQueue = 0;
        }
    }
    return 0;
}
