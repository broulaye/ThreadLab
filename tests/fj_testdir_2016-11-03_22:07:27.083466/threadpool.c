#include <pthread.h>
#include <stdlib.h>
#include "threadpool.h"
#include "list.h"
#include <string.h>
#include <stdio.h>

static __thread int internalExternal;



static void * thread_runner(void *);
struct thread_pool {
    pthread_mutex_t globalQueueLock;
    pthread_mutex_t shutDown_Lock;
    pthread_mutex_t jobCounter_Lock;
    pthread_cond_t jobCounter_cond;
    int numOfJobs;
	struct list globalQueue;//type of futures
	int nthreads;
	bool shutDown;
	struct thread_struct * threads;
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
    pthread_mutex_t futureStateLock;
    pthread_cond_t future_cond;

};

struct thread_struct {
	pthread_t thread;
	int threadNum;
	pthread_mutex_t queueLock;
	struct list queue;//type of futures
	struct thread_pool *pool;
};


static inline void run_future(struct future *f) {


    f->result = f->task(f->pool, f->data);

    pthread_mutex_lock(&f->futureStateLock);

    f->futureState = DONE;
    pthread_cond_signal(&f->future_cond);


    pthread_mutex_unlock(&f->futureStateLock);

}

/* Create a new thread pool with no more than n threads. */
struct thread_pool * thread_pool_new(int nthreads) {
	struct thread_pool *pool = malloc (sizeof(struct thread_pool));
	pool->threads = malloc(nthreads * sizeof(struct thread_struct));
	list_init(&pool->globalQueue);
	pool->nthreads = nthreads;
	pool->shutDown = false;
	pool->numOfJobs = 0;
	internalExternal = 0;
	pthread_mutex_init(&pool->globalQueueLock, NULL);
    pthread_mutex_init(&pool->jobCounter_Lock, NULL);
    pthread_mutex_init(&pool->shutDown_Lock, NULL);
    pthread_cond_init(&pool->jobCounter_cond, NULL);

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

    internalExternal = thread->threadNum;

    pthread_mutex_lock(&thread->pool->shutDown_Lock);

    while(!thread->pool->shutDown){
        pthread_mutex_lock(&thread->pool->jobCounter_Lock);
        while(thread->pool->numOfJobs < 1) {
            pthread_cond_wait(&thread->pool->jobCounter_cond, &thread->pool->jobCounter_Lock);
        }
        pthread_mutex_unlock(&thread->pool->jobCounter_Lock);
        pthread_mutex_unlock(&thread->pool->shutDown_Lock);

        pthread_mutex_lock(&thread->queueLock);

        if(!list_empty(&thread->queue)) {
            pthread_mutex_lock(&list_entry(list_begin(&thread->queue),struct future, e)->futureStateLock);
            struct list_elem *elem = list_pop_front(&thread->queue);
            struct future *f = list_entry(elem, struct future, e);
            pthread_mutex_lock(&thread->pool->jobCounter_Lock);
            thread->pool->numOfJobs--;
            pthread_mutex_unlock(&thread->pool->jobCounter_Lock);
            f->futureState = WORKING;
            pthread_mutex_unlock(&f->futureStateLock);
            pthread_mutex_unlock(&thread->queueLock);
            run_future(f);
        }
        else  {
            pthread_mutex_unlock(&thread->queueLock);

            pthread_mutex_lock(&thread->pool->globalQueueLock);

            if(!list_empty(&thread->pool->globalQueue)){
                pthread_mutex_lock(&list_entry(list_begin(&thread->pool->globalQueue),struct future, e)->futureStateLock);
                struct list_elem *elem = list_pop_front(&thread->pool->globalQueue);
                struct future *f = list_entry(elem, struct future, e);
                pthread_mutex_lock(&thread->pool->jobCounter_Lock);
                thread->pool->numOfJobs--;
                pthread_mutex_unlock(&thread->pool->jobCounter_Lock);
                f->futureState = WORKING;
                pthread_mutex_unlock(&f->futureStateLock);
                pthread_mutex_unlock(&thread->pool->globalQueueLock);
                run_future(f);

            }
            else {
                pthread_mutex_unlock(&thread->pool->globalQueueLock);
                /**Stealing Implementation*/
                for(int i = thread->threadNum-1; i >= 0; i--) {

                    struct thread_struct * vic = &thread->pool->threads[i];
                    pthread_mutex_lock(&vic->queueLock);
                    if(!list_empty(&vic->queue)) {
                        pthread_mutex_lock(&list_entry(list_rbegin(&vic->queue),struct future, e)->futureStateLock);
                        struct list_elem *elem = list_pop_back(&vic->queue);
                        struct future *f = list_entry(elem, struct future, e);
                        pthread_mutex_lock(&thread->pool->jobCounter_Lock);
                        thread->pool->numOfJobs--;
                        pthread_mutex_unlock(&thread->pool->jobCounter_Lock);
                        f->futureState = WORKING;
                        f->threadRunningF = internalExternal;
                        pthread_mutex_unlock(&f->futureStateLock);
                        pthread_mutex_unlock(&vic->queueLock);
                        run_future(f);
                    }
                    else {
                        pthread_mutex_unlock(&vic->queueLock);
                    }
                }
            }
        }
        pthread_mutex_lock(&thread->pool->shutDown_Lock);
    }

    pthread_mutex_unlock(&thread->pool->shutDown_Lock);
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

    pthread_mutex_lock(&pool->shutDown_Lock);
    pool->shutDown = true;
    pool->numOfJobs = 1;
    pthread_cond_broadcast(&pool->jobCounter_cond);
    pthread_mutex_unlock(&pool->shutDown_Lock);
	int rc;
	for (int i = 0; i < pool->nthreads; i++) {
		rc = pthread_join(pool->threads[i].thread, NULL);
		if (rc) {
			printf("ERROR: return code from pthread_join() is %d\n", rc);
			exit(-1);
		}
	}

	free(pool->threads);
	free(pool);
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
            struct future *f= malloc(sizeof(struct future));
            f->task = task;
            f->pool = pool;
            f->data = data;
            f->futureState = UNSTARTED;
            f->result = NULL;
            f->threadRunningF = -1;
            pthread_mutex_init(&f->futureStateLock, NULL);
            pthread_cond_init(&f->future_cond, NULL);
            if(internalExternal == 0) {

                pthread_mutex_lock(&pool->globalQueueLock);

                list_push_front(&pool->globalQueue, &f->e);
                pthread_mutex_lock(&pool->jobCounter_Lock);
                pool->numOfJobs++;
                pthread_cond_broadcast(&pool->jobCounter_cond);
                pthread_mutex_unlock(&pool->jobCounter_Lock);
                pthread_mutex_unlock(&pool->globalQueueLock);

            }
            else {
                pthread_mutex_lock(&pool->threads[internalExternal-1].queueLock);
                list_push_front(&pool->threads[internalExternal-1].queue, &f->e);
                pthread_mutex_lock(&pool->jobCounter_Lock);
                pool->numOfJobs++;
                pthread_cond_broadcast(&pool->jobCounter_cond);
                pthread_mutex_unlock(&pool->jobCounter_Lock);
                pthread_mutex_unlock(&pool->threads[internalExternal-1].queueLock);



            }
            return f;

}

/* Make sure that the thread pool has completed the execution
 * of the fork join task this future represents.
 *
 * Returns the value returned by this task.
 */
void * future_get(struct future *f) {


        if(internalExternal == 0){
            pthread_mutex_lock(&f->pool->globalQueueLock);
            pthread_mutex_lock(&f->futureStateLock);
            if(f->futureState == DONE) {
                    pthread_mutex_unlock(&f->futureStateLock);
                    pthread_mutex_unlock(&f->pool->globalQueueLock);
                    return f->result;
            }
            else if(f->futureState == UNSTARTED){
                    list_remove(&f->e);
                    pthread_mutex_lock(&f->pool->jobCounter_Lock);
                    f->pool->numOfJobs--;
                    pthread_mutex_unlock(&f->pool->jobCounter_Lock);
                    f->futureState = WORKING;
                    pthread_mutex_unlock(&f->futureStateLock);
                    pthread_mutex_unlock(&f->pool->globalQueueLock);
                    run_future(f);
                    return f->result;
            }
            else {
                pthread_mutex_unlock(&f->pool->globalQueueLock);
                while (f->futureState != DONE) {
                    pthread_cond_wait(&f->future_cond, &f->futureStateLock);
                }
                pthread_mutex_unlock(&f->futureStateLock);
            }
        }
        else {
            pthread_mutex_lock(&f->pool->threads[internalExternal-1].queueLock);
            pthread_mutex_lock(&f->futureStateLock);
            if(f->futureState == DONE) {
                    pthread_mutex_unlock(&f->futureStateLock);
                    pthread_mutex_unlock(&f->pool->threads[internalExternal-1].queueLock);
                    //pthread_mutex_unlock(&f->pool->globalQueueLock);
                    return f->result;
            }
            else if(f->futureState == UNSTARTED){
                    list_remove(&f->e);
                    pthread_mutex_lock(&f->pool->jobCounter_Lock);
                    f->pool->numOfJobs--;
                    pthread_mutex_unlock(&f->pool->jobCounter_Lock);
                    pthread_mutex_lock(&f->pool->jobCounter_Lock);
                    f->pool->numOfJobs--;
                    pthread_mutex_unlock(&f->pool->jobCounter_Lock);
                    f->futureState = WORKING;
                    pthread_mutex_unlock(&f->futureStateLock);
                    pthread_mutex_unlock(&f->pool->threads[internalExternal-1].queueLock);
                    run_future(f);
                    return f->result;
            }
            else {
                /**Helping Implementation*/
                 pthread_mutex_unlock(&f->pool->threads[internalExternal-1].queueLock);
                 struct thread_struct * vic = &f->pool->threads[f->threadRunningF-1];
                 while(f->futureState != DONE) {
                    pthread_mutex_unlock(&f->futureStateLock);
                    pthread_mutex_lock(&vic->queueLock);
                    if(!list_empty(&vic->queue)) {
                        struct future *vic_fut = list_entry(list_pop_back(&vic->queue), struct future, e);
                        vic_fut->futureState = WORKING;
                        vic_fut->threadRunningF = internalExternal;
                        pthread_mutex_unlock(&vic->queueLock);
                        run_future(vic_fut);
                    }
                    else {
                        pthread_mutex_unlock(&vic->queueLock);
                    }
                    pthread_mutex_lock(&f->futureStateLock);
                 }
                 pthread_mutex_unlock(&f->futureStateLock);


            }

	}
	return f->result;

}



/* Deallocate this future.  Must be called after future_get() */
void future_free(struct future * fut) {

	free(fut);
}

