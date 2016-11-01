#include <pthread.h>
#include <stdlib.h>
#include "threadpool.h"
#include "list.h"
#include <string.h>
#include <stdio.h>

static __thread int internalExternal;
static pthread_mutex_t taskLock;
static pthread_mutex_t dataLock;
static pthread_mutex_t futureStateLock;

static pthread_mutex_t thread_Lock;
static pthread_mutex_t futureLock;
static int maxThread;
static int numOfThread;
static void * thread_runner(void *);
struct thread_pool {
    pthread_mutex_t globalQueueLock;
    pthread_mutex_t shutDown_Lock;
	struct thread_struct * threads;
	struct list globalQueue;//type of futures
	int nthreads;
	bool shutDown;
};

enum state {
    DONE, WORKING, UNSTARTED
};

struct future {
    struct list_elem e;
	struct thread_pool *pool;
	fork_join_task_t task;
	enum state futureState;
	void *data;
	void *result;
	int threadRunningF;

};

struct thread_struct {
	pthread_t thread;
	int threadNum;
	pthread_mutex_t queueLock;
	struct list queue;//type of futures
	struct thread_pool *pool;
};

static void run_future(struct future *f) {
    f->futureState = WORKING;
    f->result = f->task(f->pool, f->data);
    f->futureState = DONE;

}

/* Create a new thread pool with no more than n threads. */
struct thread_pool * thread_pool_new(int nthreads) {
	struct thread_pool *pool = malloc (sizeof(struct thread_pool));
	pool->threads = malloc(nthreads * sizeof(struct thread_struct));
	list_init(&pool->globalQueue);
	pool->nthreads = nthreads;
	pool->shutDown = false;
	maxThread = nthreads;
	numOfThread = 0;
	internalExternal = 0;
	pthread_mutex_init(&pool->globalQueueLock, NULL);
    pthread_mutex_init(&futureStateLock, NULL);
    pthread_mutex_init(&futureLock, NULL);
    pthread_mutex_init(&pool->shutDown_Lock, NULL);
    pthread_mutex_init(&dataLock, NULL);
    pthread_mutex_init(&taskLock, NULL);
    pthread_mutex_init(&thread_Lock, NULL);
	int rc;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	for (int i = 0; i < nthreads; i++) {
        pthread_mutex_init(&pool->threads[i].queueLock, NULL);
        list_init(&pool->threads[i].queue);
        pool->threads[i].pool = pool;
        pool->threads[i].threadNum = i+1;
		rc = pthread_create(&(pool->threads[i].thread), &attr, thread_runner, (void *)(pool->threads + i)); // Params will need to be changed
		// add mutex lock to thread_struct, init it, etc.

		if (rc) {
			printf("ERROR: return code from pthread_create() is %d\n", rc);
			exit(-1);
		}
	}
	pthread_attr_destroy(&attr);

	return pool;
}



static void * thread_runner(void *t) {
    pthread_mutex_lock(&thread_Lock);
    struct thread_struct * thread = (struct thread_struct *) t;
    internalExternal = thread->threadNum;
    pthread_mutex_unlock(&thread_Lock);
    pthread_mutex_lock(&thread->pool->shutDown_Lock);
    while(!thread->pool->shutDown){
        pthread_mutex_unlock(&thread->pool->shutDown_Lock);
        pthread_mutex_lock(&thread->queueLock);
        if(list_size(&thread->queue) > 0) {
            printf("Removing from queue %d\n", internalExternal);
            struct list_elem *elem = list_pop_front(&thread->queue);
            pthread_mutex_unlock(&thread->queueLock);
            struct future *f = list_entry(elem, struct future, e);
            run_future(f);
        }
        else {
            pthread_mutex_lock(&thread->pool->globalQueueLock);
            if(list_size(&thread->pool->globalQueue) > 0){
                printf("Removing from global queue \n");
                struct list_elem *elem = list_pop_back(&thread->pool->globalQueue);
                pthread_mutex_unlock(&thread->pool->globalQueueLock);
                struct future *f = list_entry(elem, struct future, e);
                run_future(f);
            }
            pthread_mutex_unlock(&thread->queueLock);
            pthread_mutex_unlock(&thread->pool->globalQueueLock);
            pthread_mutex_lock(&thread->pool->shutDown_Lock);
        }
    }
    pthread_mutex_unlock(&thread->pool->globalQueueLock);
    pthread_mutex_unlock(&thread->queueLock);
    pthread_mutex_unlock(&thread->pool->shutDown_Lock);
    return 0;
}



/*
 * Shutdown this thread pool in an orderly fashion.
 * Tasks that have been submitted but not executed may or
 * may not be executed.
 *
 * Deallocate the thread pool object before returning.
 */
void thread_pool_shutdown_and_destroy(struct thread_pool * pool) {
//also destroy mutex lock
    //pthread_mutex_lock(&queueLock);
    pthread_mutex_lock(&pool->shutDown_Lock);
    pool->shutDown = true;
    pthread_mutex_unlock(&pool->shutDown_Lock);
	int rc;
	for (int i = 0; i < pool->nthreads; i++) {
		rc = pthread_join(pool->threads[i].thread, NULL);
		if (rc) {
			printf("ERROR: return code from pthread_join() is %d\n", rc);
			exit(-1);
		}
	}


	free(pool);
	//pthread_mutex_unlock(&queueLock);
}

/*
 * Submit a fork join task to the thread pool and return a
 * future.  The returned future can be used in future_get()
 * to obtain the result.
 * 'pool' - the pool to which to submit
 * 'task' - the task to be submitted.
 * 'data' - data to be passed to the task's function
 *
 * Returns a future representing this computation.
 */
struct future * thread_pool_submit(
        struct thread_pool *pool,
        fork_join_task_t task,
        void * data) {

            /**
                struct list_elem e;
	struct thread_pool *pool;
	fork_join_task_t task;
	enum state futureState;
	void *data;
	void *result;
            */

            struct future *f= malloc(sizeof(struct future));
            f->task = task;
            f->pool = pool;
            f->data = data;
            f->futureState = UNSTARTED;
            f->result = NULL;
            f->threadRunningF = internalExternal;
            pthread_mutex_lock(&pool->globalQueueLock);
            if(internalExternal == 0) {
                //external submission
                printf("pushing to global Queue\n");
                list_push_front(&pool->globalQueue, &f->e);
                printf("Unlocking queue lock\n");
                pthread_mutex_unlock(&pool->globalQueueLock);

            }
            else {
                pthread_mutex_lock(&pool->threads[internalExternal].queueLock);
                printf("pushing to local Queue %d\n", internalExternal);
                list_push_front(&pool->threads[internalExternal].queue, &f->e);
                printf("Unlocking queue lock\n");
                pthread_mutex_unlock(&pool->threads[internalExternal].queueLock);
                //internal submission
                //pthread_t thread;
                //pthread_create(&thread, NULL, thread_runner, (void *) thread_struct);
                //list_remove(&f->e);
                //printf("Unlocking queue lock\n");
                //pthread_mutex_unlock(&queueLock);
                //run_future(f);



            }
            printf("Unlocking queue lock\n");
            pthread_mutex_unlock(&pool->globalQueueLock);
            pthread_mutex_unlock(&pool->threads[internalExternal].queueLock);
            return f;

}

/* Make sure that the thread pool has completed the execution
 * of the fork join task this future represents.
 *
 * Returns the value returned by this task.
 */
void * future_get(struct future *f) {
    pthread_mutex_lock(&futureStateLock);
    //pthread_mutex_lock(&queueLock);
    void *result;
    if(f->futureState == DONE) {
        pthread_mutex_unlock(&futureStateLock);
        //pthread_mutex_unlock(&queueLock);
        result =  f->result;
        future_free(f);
        return result;
    }
    else if(f->futureState == WORKING) {
        pthread_mutex_unlock(&futureStateLock);
        //pthread_mutex_unlock(&queueLock);
        //Shoudln't happen yet
        printf("Working task\n");
        //run_future(f);
        pthread_mutex_lock(&futureLock);
        result =  f->result;
        future_free(f);
        pthread_mutex_unlock(&futureLock);
        return result;
        if(internalExternal == 0) {

        }
        else {

        }
    }
    else {

        pthread_mutex_unlock(&futureStateLock);
        if(internalExternal == 0) {
            printf("Removing from %d\n", internalExternal);
            pthread_mutex_lock(&f->pool->globalQueueLock);
            list_remove(&f->e);
            pthread_mutex_unlock(&f->pool->globalQueueLock);
        }
        else {
            printf("Removing from %d\n", internalExternal);
            pthread_mutex_lock(&f->pool->threads[internalExternal].queueLock);
            list_remove(&f->e);
            pthread_mutex_unlock(&f->pool->threads[internalExternal].queueLock);
        }


        run_future(f);



        result =  f->result;
        pthread_mutex_lock(&futureLock);
        future_free(f);
        pthread_mutex_unlock(&futureLock);
        return result;
    }

}



/* Deallocate this future.  Must be called after future_get() */
void future_free(struct future * fut) {
	free(fut);
}

