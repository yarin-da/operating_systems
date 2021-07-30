#include "threadPool.h"
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

#define ERROR   (-1)
#define SUCCESS (0)

// error handling
static void printError(const char *msg);
static void onError(ThreadPool *threadPool, const char *msg);

// data initialization and destruction
static void initThreads(ThreadPool *threadPool);
static void initQueue(ThreadPool *threadPool);
static void destroyQueue(ThreadPool *threadPool);
static void destroyThreads(ThreadPool *threadPool);
static void destroyPool(ThreadPool *threadPool);

// wrapper functions that handle errors
static void tpMutexInit(ThreadPool *threadPool, pthread_mutex_t *mutex);
static void tpLock(bool_t lock, ThreadPool *threadPool, pthread_mutex_t *mutex);
static void tpCondInit(ThreadPool *threadPool, pthread_cond_t *cond);
static void tpWait(ThreadPool *threadPool, pthread_cond_t *cond, pthread_mutex_t *mutex);
static void tpSignal(ThreadPool *threadPool, pthread_cond_t *cond);
static void tpBroadcast(ThreadPool *threadPool, pthread_cond_t *cond);

 // task handling
static Task *createTask(void (*computeFunc) (void *), void* param);
static void addTask(ThreadPool *threadPool, void (*computeFunc) (void *), void* param);
static Task *fetchTask(ThreadPool *threadPool);
static void doTask(ThreadPool *threadPool, Task *task);
static void *threadLoop(void *arg);

// task cleanup handling
static void waitForRunningTasks(ThreadPool *threadPool);
static void waitForPendingTasks(ThreadPool *threadPool);


// a wrapper for perror
static void printError(const char *msg) {
	perror(msg);
}

// destroys all data related to the queue
static void destroyQueue(ThreadPool *threadPool) {
	tpLock(TRUE, threadPool, &threadPool->queueLock);
	while (!osIsQueueEmpty(threadPool->tasks)) {
		free(osDequeue(threadPool->tasks));
	}
	osDestroyQueue(threadPool->tasks);
	threadPool->tasks = NULL;
	tpLock(FALSE, threadPool, &threadPool->queueLock);
	pthread_mutex_destroy(&threadPool->queueLock);
	pthread_cond_destroy(&threadPool->queueCond);
}

static void signalThreadsToFinish(ThreadPool *threadPool) {
	tpLock(TRUE, threadPool, &threadPool->tpMutex);
	threadPool->finish = TRUE;
	// wake all sleeping threads
	// so they can check threadPool->finish and terminate
	tpBroadcast(threadPool, &threadPool->queueCond);
	tpLock(FALSE, threadPool, &threadPool->tpMutex);
}

// destroys all data related to the threads in the pool
// also, makes sure that all threads exit
static void destroyThreads(ThreadPool *threadPool) {
	signalThreadsToFinish(threadPool);

	tpLock(TRUE, threadPool, &threadPool->threadFinLock);
	while (threadPool->finished < threadPool->size) {
		tpWait(threadPool, &threadPool->threadFinCond, &threadPool->threadFinLock);
	}
	tpLock(FALSE, threadPool, &threadPool->threadFinLock);

	free(threadPool->threads);
	threadPool->threads = NULL;
}

// destroys the whole thread pool data
static void destroyPool(ThreadPool *threadPool) {
	// we can assume that all data has been allocated
	// otherwise, the program would have failed with an error
	destroyThreads(threadPool);
	destroyQueue(threadPool);
	
	pthread_mutex_destroy(&threadPool->threadFinLock);
	pthread_cond_destroy(&threadPool->threadFinCond);

	pthread_mutex_destroy(&threadPool->tpMutex);
	pthread_cond_destroy(&threadPool->finTaskCond);
	pthread_cond_destroy(&threadPool->pendingCond);
	
	free(threadPool);
}

// prints an error, cleans up the data, and exits with an error code
static void onError(ThreadPool *threadPool, const char *msg) {
	printError(msg);
	destroyPool(threadPool);
	exit(ERROR);
}

static void tpMutexInit(ThreadPool *threadPool, pthread_mutex_t *mutex) {
	if (pthread_mutex_init(mutex, NULL) != SUCCESS) {
		onError(threadPool, "Error in pthread_mutex_init");
	}
}

static void tpCondInit(ThreadPool *threadPool, pthread_cond_t *cond) {
	if (pthread_cond_init(cond, NULL) != SUCCESS) {
		onError(threadPool, "Error in pthread_cond_init");
	}
}

// lock if 'lock' is TRUE, otherwise unlock
static void tpLock(bool_t lock, ThreadPool *threadPool, pthread_mutex_t *mutex) {
	if (lock) {
		if (pthread_mutex_lock(mutex) != SUCCESS) {
			onError(threadPool, "Error in pthread_mutex_lock");
		}
	} else {
		if (pthread_mutex_unlock(mutex) != SUCCESS) {
			onError(threadPool, "Error in pthread_mutex_unlock");
		}
	}
}

static void tpWait(ThreadPool *threadPool, pthread_cond_t *cond, pthread_mutex_t *mutex) {
	if (pthread_cond_wait(cond, mutex) != SUCCESS) {
		onError(threadPool, "Error in pthread_cond_wait");
	}
}

static void tpSignal(ThreadPool *threadPool, pthread_cond_t *cond) {
	if (pthread_cond_signal(cond) != SUCCESS) {
		onError(threadPool, "Error in pthread_cond_signal");
	}
}

static void tpBroadcast(ThreadPool *threadPool, pthread_cond_t *cond) {
	if (pthread_cond_broadcast(cond) != SUCCESS) {
		onError(threadPool, "Error in pthread_cond_broadcast");
	}
}

// allocate memory for a new task
static Task *createTask(void (*computeFunc) (void *), void* param) {
	Task *task = malloc(sizeof(Task));
	if (task != NULL) {
		task->func = computeFunc;
		task->param = param;
	}
	return task;
}

static void destroyTask(Task *task) {
	free(task);
}

// notify that you're running a task
static void notifyStartTask(ThreadPool *threadPool) {
	tpLock(TRUE, threadPool, &threadPool->tpMutex);
	threadPool->running += 1;
	tpLock(FALSE, threadPool, &threadPool->tpMutex);
}

static void notifyFinishedTask(ThreadPool *threadPool) {
	tpLock(TRUE, threadPool, &threadPool->tpMutex);
	threadPool->running -= 1;
	// signal all threads that a task has been finished
	tpSignal(threadPool, &threadPool->finTaskCond);
	tpSignal(threadPool, &threadPool->pendingCond);
	tpLock(FALSE, threadPool, &threadPool->tpMutex);
}

static void doTask(ThreadPool *threadPool, Task *task) {
	notifyStartTask(threadPool);

	// do the task
	task->func(task->param);
	destroyTask(task);

	notifyFinishedTask(threadPool);
}

static Task *fetchTask(ThreadPool *threadPool) {
	tpLock(TRUE, threadPool, &threadPool->queueLock);
	// check if there's a task in the queue
	while (!threadPool->finish && osIsQueueEmpty(threadPool->tasks)) {
		// sleep if there are no tasks
		tpWait(threadPool, &threadPool->queueCond, &threadPool->queueLock);
	}
	// return the task (or NULL if you've been told to finish execution)
	Task *task = threadPool->finish ? NULL : osDequeue(threadPool->tasks);
	tpLock(FALSE, threadPool, &threadPool->queueLock);
	return task;
}

// notify the pool that you're finished
static void notifyFinished(ThreadPool *threadPool) {
	tpLock(TRUE, threadPool, &threadPool->threadFinLock);
	threadPool->finished++;
	tpSignal(threadPool, &threadPool->threadFinCond);
	tpLock(FALSE, threadPool, &threadPool->threadFinLock);
}

// the 'main' function of the threads in the pool
static void *threadLoop(void *arg) {
	ThreadPool *threadPool = arg;
	// as long as you're supposed to run
	while (!threadPool->finish) {
		Task *task = fetchTask(threadPool);
		if (task != NULL) {
			doTask(threadPool, task);
		}
	}

	notifyFinished(threadPool);
}

static void initQueue(ThreadPool *threadPool) {
	tpMutexInit(threadPool, &threadPool->queueLock);
	tpCondInit(threadPool, &threadPool->queueCond);

	threadPool->tasks = osCreateQueue();
	if (threadPool->tasks == NULL) {
		onError(threadPool, "Out of memory");
	}
}

static void initThreads(ThreadPool *threadPool) {
	threadPool->threads = malloc(sizeof(pthread_t) * threadPool->size);
	if (threadPool->threads == NULL) {
		onError(threadPool, "Out of memory");
	}
	
	pthread_attr_t attr;
	// create an attr to make threads detachable
	// otherwise valgrind will complain about possible memory leaks
	if (pthread_attr_init(&attr) == ERROR) {
		onError(threadPool, "Error in pthread_attr_init");	
	}
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED) == ERROR) {
		onError(threadPool, "Error in pthread_attr_setdetachstate");
	}
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	int i;
	for (i = 0; i < threadPool->size; i++) {
		pthread_t *curr = &threadPool->threads[i];
		if (pthread_create(curr, &attr, threadLoop, (void *)threadPool) != SUCCESS) {
			onError(threadPool, "Error in pthread_create");
		}
	}
	if (pthread_attr_destroy(&attr) == ERROR) {
		onError(threadPool, "Error in pthread_attr_destroy");	
	}
}

ThreadPool *tpCreate(int numOfThreads) {
	ThreadPool *threadPool = malloc(sizeof(ThreadPool));
	if (threadPool == NULL) {
		onError(threadPool, "Out of memory");
	}

	threadPool->finish = FALSE;
	threadPool->destroyed = FALSE;
	threadPool->size = numOfThreads;
	threadPool->running = 0;
	threadPool->finished = 0;

	tpMutexInit(threadPool, &threadPool->threadFinLock);
	tpCondInit(threadPool, &threadPool->threadFinCond);
	tpMutexInit(threadPool, &threadPool->tpMutex);
	tpCondInit(threadPool, &threadPool->finTaskCond);
	tpCondInit(threadPool, &threadPool->pendingCond);

	initQueue(threadPool);
	initThreads(threadPool);

	return threadPool;
}

static void addTask(ThreadPool *threadPool, void (*computeFunc) (void *), void* param) {
	Task *task = createTask(computeFunc, param);
	if (task == NULL) {
		onError(threadPool, "Out of memory");
	}

	tpLock(TRUE, threadPool, &threadPool->queueLock);
	osEnqueue(threadPool->tasks, task);
	tpLock(FALSE, threadPool, &threadPool->queueLock);
}

static void awakeThread(ThreadPool *threadPool) {
	tpSignal(threadPool, &threadPool->queueCond);
}

static bool_t isDestroyed(ThreadPool *threadPool) {
	tpLock(TRUE, threadPool, &threadPool->tpMutex);
	bool_t ret = threadPool->destroyed;
	tpLock(FALSE, threadPool, &threadPool->tpMutex);
	return ret;
}

int tpInsertTask(ThreadPool *threadPool, void (*computeFunc) (void *), void* param) {
	if (isDestroyed(threadPool)) {
		return ERROR;
	}

	addTask(threadPool, computeFunc, param);
	awakeThread(threadPool);
	return SUCCESS;
}

static void waitForRunningTasks(ThreadPool *threadPool) {
	pthread_mutex_lock(&threadPool->tpMutex);
	while (threadPool->running > 0) {
		tpWait(threadPool, &threadPool->finTaskCond, &threadPool->tpMutex);
	}
	pthread_mutex_unlock(&threadPool->tpMutex);
}

static void waitForPendingTasks(ThreadPool *threadPool) {
	pthread_mutex_lock(&threadPool->queueLock);
	while (!osIsQueueEmpty(threadPool->tasks)) {
		tpWait(threadPool, &threadPool->pendingCond, &threadPool->queueLock);
	}
	pthread_mutex_unlock(&threadPool->queueLock);
}

static bool_t setDestroyed(ThreadPool *threadPool) {
	int hasBeenDestroyed;
	tpLock(TRUE, threadPool, &threadPool->tpMutex);
	// if not destroyed yet - mark it as destroyed
	hasBeenDestroyed = threadPool->destroyed;
	if (!hasBeenDestroyed) {
		threadPool->destroyed = TRUE;
	}
	tpLock(FALSE, threadPool, &threadPool->tpMutex);
	return hasBeenDestroyed;
}

void tpDestroy(ThreadPool *threadPool, int shouldWaitForTasks) {
	// don't let this function be called more than once
	// let only the first thread pass
	if (setDestroyed(threadPool)) {
		return;
	}

	// finish all tasks in the queue
	if (shouldWaitForTasks) {
		waitForPendingTasks(threadPool);
	}

	// tell threads to finish their execution
	// and wait for all running tasks
	signalThreadsToFinish(threadPool);
	waitForRunningTasks(threadPool);

	// get rid of the all the allocated memory
	destroyPool(threadPool);
}