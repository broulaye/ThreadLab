#include <pthread.h>
#include <stdlib.h>
#include "threadpool.h"
#include "list.h"
#include <string.h>
#include <stdio.h>

static __thread int internalExternal;
static pthread_mutex_t queueLock;
static pthread_mutex_t taskLock;
static pthread_mutex_t dataLock;
static pthread_mutex_t futureStateLock;
static pthread_mutex_t shutDown_Lock;
static int maxThread;
static int numOfThread;
static void * thread_runner(void *);
struct thread_pool {
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

};

struct thread_struct {
	pthread_t thread;
	int threadNum;
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
	pool->nthreads = nthreads;
	pool->shutDown = false;
	maxThread = nthreads;
	numOfThread = 0;
	internalExternal = 0;
    pthread_mutex_init(&futureStateLock, NULL);
    pthread_mutex_init(&shutDown_Lock, NULL);
    pthread_mutex_init(&dataLock, NULL);
    pthread_mutex_init(&taskLock, NULL);
    pthread_mutex_init(&queueLock, NULL);

	int rc;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	for (int i = 0; i < nthreads; i++) {
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
    struct thread_struct * thread = (struct thread_struct *) t;
    pthread_mutex_lock(&shutDown_Lock);
    while(!thread->pool->shutDown){
        pthread_mutex_unlock(&shutDown_Lock);
        if(list_size(&thread->queue) > 0) {
            struct list_elem *elem = list_pop_front(&thread->queue);
            struct future *f = list_entry(elem, struct future, e);
            run_future(f);
        }
        else if(list_size(&thread->pool->globalQueue) > 0){
            struct list_elem *elem = list_pop_back(&thread->pool->globalQueue);
            struct future *f = list_entry(elem, struct future, e);
            run_future(f);
        }
        pthread_mutex_lock(&shutDown_Lock);
        return NULL;
    }
    pthread_mutex_unlock(&shutDown_Lock);
    return NULL;
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
    pthread_mutex_lock(&shutDown_Lock);
    pool->shutDown = true;
    pthread_mutex_unlock(&shutDown_Lock);
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
            //pthread_mutex_lock(&queueLock);
            struct future *f= malloc(sizeof(struct future));
            f->task = task;
            f->pool = pool;
            f->data = data;
            f->futureState = UNSTARTED;
            if(internalExternal == 0) {
                //external submission
                list_push_front(&pool->globalQueue, &f->e);

            }
            else {
                //internal submission
                //pthread_t thread;
                //pthread_create(&thread, NULL, thread_runner, (void *) thread_struct);

            }
            //pthread_mutex_unlock(&queueLock);*/
            return f;

}

/* Make sure that the thread pool has completed the execution
 * of the fork join task this future represents.
 *
 * Returns the value returned by this task.
 */
void * future_get(struct future *f) {
    //pthread_mutex_lock(&queueLock);
    pthread_mutex_lock(&futureStateLock);
    if(f->futureState == DONE) {
        pthread_mutex_unlock(&futureStateLock);
        return f->result;
    }
    else if(f->futureState == WORKING) {
        pthread_mutex_unlock(&futureStateLock);
        //Shoudln't happen yet
        return NULL;
        if(internalExternal == 0) {

        }
        else {

        }
    }
    else {
        pthread_mutex_unlock(&futureStateLock);
        list_remove(&f->e);
        run_future(f);
        //pthread_mutex_unlock(&queueLock);
        return f->result;
    }

}



/* Deallocate this future.  Must be called after future_get() */
void future_free(struct future * fut) {
	free(fut);
	//free(fut);
}

