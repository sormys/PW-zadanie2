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
    int id;
    pid_t pid;
    pthread_t thread;
    char out[MAX_LINE];
    pthread_mutex_t outSem;
    char err[MAX_LINE];
    pthread_mutex_t errSem;
    char** argv;
    // char* line;
};

struct lineInfo {
    pthread_mutex_t mutex;
    char* buf;
    int descriptor;
};

struct Task tasks[MAX_N_TASKS];
bool busy = false;
pthread_mutex_t busyQueue;
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
    while (true) {
        if (debug)
            printf("OUT/ERR czyta linie\n");
        if (fgets(line, MAX_LINE, fileToRead) == NULL) {
            break;
        }
        strtok(line, "\n");
        if (debug)
            printf("PRZECZYTAL: %s\n", line);
        if (debug)
            printf("OUT/ERR probuje ja zapisac\n");
        pthread_mutex_lock(&info->mutex);
        strcpy(info->buf, line);
        // info->buf[strlen(line) - 1] = '\0';
        pthread_mutex_unlock(&info->mutex);
    }

    if (debug)
        printf("OUT/ERR konczy...\n");
    fclose(fileToRead);
    // sleep(10);
    if (debug)
        printf("OUT/ERR skonczyl\n");
    return NULL;
}

void finishInfo(int taskNo, int exitStatus)
{
    char info[MAX_END_OUT];
    if (WIFEXITED(exitStatus)) {
        sprintf(info, "Task %d ended: status %d.\n", taskNo, exitStatus);
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
    // for (int i = 0; (task->argv)[i] != NULL; ++i)
    //    if(debug) print("task->argv[%d]: %s\n", i, (task->argv)[i]);
    // if(debug) print("task->argv[1]: %s\n");
    ASSERT_SYS_OK(pipe(fdOut));
    int fdErr[2];
    ASSERT_SYS_OK(pipe(fdErr));
    pid_t pid = fork();
    ASSERT_SYS_OK(pid);
    if (!pid) {
        // child?
        if (debug)
            printf("Bachor stworzony\n");
        ASSERT_SYS_OK(close(fdOut[0]));
        ASSERT_SYS_OK(close(fdErr[0]));
        if (debug)
            printf("Bachor odjebal pipe\n");
        ASSERT_SYS_OK(dup2(fdOut[1], STDOUT_FILENO));
        // if(debug) print("Bachor odjebal dup2\n");
        ASSERT_SYS_OK(dup2(fdErr[1], STDERR_FILENO));
        // if(debug) print("Bachor odjebal dup2\n");
        ASSERT_SYS_OK(close(fdOut[1]));
        ASSERT_SYS_OK(close(fdErr[1]));
        // if(debug) print("Bachor URUCHAMIA PROGRAM\n");
        //  sleep(100);
        // if(debug) print("task->argv[1]: %s\n", tas);
        // if(debug) print("LINE LINE LINE %s\n", (task->argv + 1)[0]);
        //  sleep(10);
        //  if (!strcmp(task->argv[1], "./number")) {
        //     if(debug) print("to samo\n");
        //  } else {
        //      int i = 0;
        //      while (task->argv[1][i] != '\0') {
        //         if(debug) print("%d ", task->argv[1][i++]);
        //      }
        //     if(debug) print("co innego\n");
        //  }
        // if(debug) print("__________________\n");
        // if(debug) print("(task->argv)[1]:%s\n", (task->argv)[1]);
        execvp(task->argv[1], &(task->argv[1]));
        // sleep(1);
        // execvp("./chuj", task->argv + 1);
    } else {
        printf("Task %d started, pid: %d.\n", task->id, pid);
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
        outInfo.mutex = task->outSem;
        errInfo.buf = task->err;
        errInfo.descriptor = fdErr[0];
        errInfo.mutex = task->errSem;
        int pidStatus;
        pthread_attr_t attr;
        if (debug)
            printf("Stary tworzy watki\n");
        ASSERT_ZERO(pthread_attr_init(&attr));
        ASSERT_ZERO(pthread_create(&out, &attr, readLines, &outInfo));
        ASSERT_ZERO(pthread_create(&err, &attr, readLines, &errInfo));
        if (debug)
            printf("Stary stworzyl watki\n");
        // create threads to readLines
        ASSERT_SYS_OK(waitpid(pid, &pidStatus, 0));
        if (debug)
            printf("STARY CO TY ODPIERDALASZ\n");
        ASSERT_ZERO(pthread_join(out, &outExitStatus));
        ASSERT_ZERO(pthread_join(err, &errExitStatus));
        free_split_string(task->argv);
        // mutex
        // char info[MAX_END_OUT];
        // TODO: QUEUE
        finishInfo(task->id, pidStatus);
        // if (WIFEXITED(pidStatus)) {
        //     printf("Task %d ended: status %d.\n", task->id, pidStatus);
        // } else {
        //     printf("Task %d ended: signalled.\n", task->id);
        // }
        // if (busy) {
        // char* q = finished + idxFinished;
        // char* info = finishInfo(task->id, pidStatus);
        // strcpy(q, info);
        // } else {
        // if(debug) printf("%s", finishInfo(task->id, pidStatus));
        // } // mutex
    }
    return NULL;
}

int main()
{
    char line[MAX_LINE];
    // char* splitLine[MAX_TASK_LENGTH];
    char** splitLine;
    int tasksCount = 0;
    pthread_attr_t attr;
    ASSERT_ZERO(pthread_attr_init(&attr));
    ASSERT_ZERO(pthread_mutex_init(&busyQueue, NULL));
    // char* line;
    while (
        // (line = malloc(sizeof(char) * MAX_LINE)) &&
        fgets(line, MAX_LINE, stdin)) {
        strtok(line, "\n");
        ASSERT_ZERO(pthread_mutex_lock(&busyQueue));
        busy = true;
        ASSERT_ZERO(pthread_mutex_unlock(&busyQueue));
        if (!strcmp(line, "\r\n") || !strcmp(line, "\n"))
            continue;
        if (debug)
            printf("MAM WYJEBANE\n");
        // if(debug) print("line: %s\n", line);

        // if(debug) print("LINE( ");
        //  while (line[i] != '\0') {
        //     if(debug) print("%d ", line[i++]);
        //  }
        // if(debug) print(")\n");

        busy = true;
        if (debug)
            printf("chuj%d\n", 0);
        splitLine = split_string(line);
        // acually_working_split_string(splitLine, line);
        // char** splitLine = split_string(line);
        // for (int i = 0; splitLine[i] != NULL; ++i)
        //     if (debug) printf("%d: %s\n", i, splitLine[i]);
        if (!strcmp(splitLine[0], "run")) {
            tasks[tasksCount].id = tasksCount;
            if (debug)
                printf("chuj2\n");
            ASSERT_ZERO(pthread_mutex_init(&(tasks[tasksCount].outSem), NULL));
            ASSERT_ZERO(pthread_mutex_init(&(tasks[tasksCount].errSem), NULL));
            if (debug)
                printf("chuj2.5\n");
            // int i = 0;
            // while (splitLine[i] != NULL) {
            //     tasks[tasksCount].argv[i] = splitLine[i++];
            // }
            // tasks[tasksCount].argv[i] = NULL;
            // tasks[tasksCount].line = line;
            tasks[tasksCount].argv = splitLine;
            // printf("COPIED??: %p\n", tasks[tasksCount].argv);
            // copySplit(tasks[tasksCount].argv, splitLine);
            // strcpy(tasks[tasksCount].argv, splitLine);
            if (debug)
                printf("chuj3\n");
            ASSERT_ZERO(pthread_create(&(tasks[tasksCount].thread), &attr,
                runTask, &tasks[tasksCount]));
            if (debug)
                printf("chuj4\n");
            tasksCount++;
        } else if (!strcmp(splitLine[0], "out")) {
            // printf("GOT OUT COMMAND\n");
            int taskId = atoi(splitLine[1]);
            struct Task currentTask = tasks[taskId];
            pthread_mutex_lock(&(currentTask.outSem));
            printf("Task %d stdout: \'%s\'.\n", taskId, currentTask.out);
            pthread_mutex_unlock((&(currentTask.outSem)));
        } else if (!strcmp(splitLine[0], "err")) {
            int taskId = atoi(splitLine[1]);
            struct Task currentTask = tasks[taskId];
            pthread_mutex_lock(&(currentTask.errSem));
            printf("Task %d stdout: \'%s\'.\n", taskId, currentTask.err);
            pthread_mutex_unlock((&(currentTask.errSem)));
        } else if (!strcmp(splitLine[0], "kill")) {
            int taskId = atoi(splitLine[1]);
            kill(tasks[taskId].pid, SIGINT);
        }
        // else if (!strcmp(splitLine[0], "sleep")) {
        //     int seconds = atoi(splitLine[1]);
        //     ASSERT_SYS_OK(sleep(seconds));
        // }
        else if (!strcmp(splitLine[0], "quit")) {
            break;
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
    // free(line);
    void* mamWyjebane;
    for (int i = 0; i < tasksCount; i++) {
        pthread_mutex_destroy(&tasks[i].errSem);
        pthread_mutex_destroy(&tasks[i].outSem);
        pthread_join(tasks[i].thread, &mamWyjebane);
        // free(tasks[i].line);
        // tasks[i].argv// TODO: argv can override?
    }

    return 0;
}
