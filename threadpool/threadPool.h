#ifndef __THREAD_POOL__
#define __THREAD_POOL__

#include "osqueue.h"
#include <pthread.h>

typedef enum { FALSE=0, TRUE=1 } bool_t;

typedef struct {
    void (*func) (void *);
    void *param;
} Task;

typedef struct {
    // TRUE if tpDestroy has been called
    bool_t destroyed;
    // TRUE if threads should terminate
    bool_t finish;
    // number of threads in the pool
    unsigned size;
    // number of threads that are currently running a task
    unsigned running;
    // number of the threads that have terminated
    unsigned finished;
    // array of threads
    pthread_t *threads;
    // queue of tasks
    OSQueue *tasks;
    // used to track when a thread has terminated
    pthread_mutex_t threadFinLock;
    pthread_cond_t threadFinCond;
    // lock when writing to the threadpool's fields
    pthread_mutex_t tpMutex;
    // used to track when a task has finished
    pthread_cond_t finTaskCond;
    // used to track if there are pending tasks in the queue
    pthread_cond_t pendingCond;
    // used to lock/wait on queue tasks
    pthread_mutex_t queueLock;
    pthread_cond_t queueCond;
} ThreadPool;

ThreadPool* tpCreate(int numOfThreads);

void tpDestroy(ThreadPool* threadPool, int shouldWaitForTasks);

int tpInsertTask(ThreadPool* threadPool, void (*computeFunc) (void *), void* param);

#endif
