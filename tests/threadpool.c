#include <pthread.h>
#include <stdlib.h>
#include "threadpool.h"
#include "list.h"
#include <string.h>
#include <stdio.h>

static __thread struct thread_struct *cur_thread;

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
	DONE,
	WORKING,
	UNSTARTED
};

struct future {
	struct list_elem e;
	struct thread_pool *pool;
	fork_join_task_t task;
	enum state futureState;
	void *data;
	void *result;
	int threadRunningF;
	pthread_cond_t finished_cond;
};

struct thread_struct {
	pthread_t thread;
	int threadNum;
	pthread_mutex_t lock;
	struct list queue;//type of futures
	struct thread_pool *pool;
};


static void run_future(struct future *f) {
	//futureStats should always be working when run_future is called
printf("running future\n");
int t = f->threadRunningF;
printf("f->theadRunningF: %d\n", t);
	f->result = f->task(f->pool, f->data);

printf("future completed running\n");
	if (f->threadRunningF == -1) { //submitted by external to global queue
		pthread_mutex_lock(&f->pool->globalQueueLock);
	} else {
		pthread_mutex_lock(&f->pool->threads[f->threadRunningF].lock);
	}

	f->futureState = DONE;
	pthread_cond_broadcast(&f->finished_cond);

printf("broadcasting future done\n");
	if (f->threadRunningF == -1) { //submitted by external to global queue
		pthread_mutex_unlock(&f->pool->globalQueueLock);
	} else {
		pthread_mutex_unlock(&f->pool->threads[f->threadRunningF].lock);
	}
}

/* Create a new thread pool with no more than n threads. */
struct thread_pool * thread_pool_new(int nthreads) {
	struct thread_pool *pool = malloc (sizeof(struct thread_pool));
	pool->threads = malloc(nthreads * sizeof(struct thread_struct));
	list_init(&pool->globalQueue);
	pool->nthreads = nthreads;
	pool->shutDown = false;
	pthread_mutex_init(&pool->globalQueueLock, NULL);

	pthread_mutex_init(&pool->shutDown_Lock, NULL);

	cur_thread = NULL;

	int rc;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	for (int i = 0; i < nthreads; i++) {
		pthread_mutex_init(&pool->threads[i].lock, NULL);
		list_init(&pool->threads[i].queue);
		pool->threads[i].pool = pool;
		pool->threads[i].threadNum = i;
		rc = pthread_create(&(pool->threads[i].thread), &attr, thread_runner, (void *)(pool->threads + i));
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
	cur_thread = (struct thread_struct *) t;
	pthread_mutex_lock(&cur_thread->pool->shutDown_Lock);

	while(!cur_thread->pool->shutDown){

		pthread_mutex_unlock(&cur_thread->pool->shutDown_Lock);

		pthread_mutex_lock(&->lock);
		// Continue to work on local queue
		if(!list_empty(&cur_thread->queue)) {
			struct list_elem *elem = list_pop_front(&cur_thread->queue);
			struct future *f = list_entry(elem, struct future, e);
			f->futureState = WORKING;
			pthread_mutex_unlock(&cur_thread->lock);

			run_future(f);
printf("future completed running in thread_runner cur_thread\n");
		}
		else {
			pthread_mutex_unlock(&cur_thread->lock);
			pthread_mutex_lock(&cur_thread->pool->globalQueueLock);
			// Take from global queue
			if(!list_empty(&cur_thread->pool->globalQueue)){
				struct list_elem *elem = list_pop_back(&cur_thread->pool->globalQueue);
				struct future *f = list_entry(elem, struct future, e);
				f->futureState = WORKING;

				pthread_mutex_unlock(&cur_thread->pool->globalQueueLock);
				run_future(f);
printf("future completed running in thread_runner global thread\n");
			}
			else {
				pthread_mutex_unlock(&cur_thread->pool->globalQueueLock);
			}
		}
		pthread_mutex_lock(&cur_thread->pool->shutDown_Lock);
	}
	pthread_mutex_unlock(&cur_thread->pool->shutDown_Lock);
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
	if (cur_thread == NULL) {
		f->threadRunningF = -1;
	} else {
		f->threadRunningF = cur_thread->threadNum;
	}
	pthread_cond_init(&f->finished_cond, NULL);
	if(cur_thread == NULL) {
		//external submission
		pthread_mutex_lock(&pool->globalQueueLock);
		list_push_front(&pool->globalQueue, &f->e);
		pthread_mutex_unlock(&pool->globalQueueLock);
	}
	else {
		pthread_mutex_lock(&cur_thread->lock);
		list_push_front(&cur_thread->queue, &f->e);
		pthread_mutex_unlock(&cur_thread->lock);
	}
	return f;
}

/* Make sure that the thread pool has completed the execution
 * of the fork join task this future represents.
 *
 * Returns the value returned by this task.
 */
void * future_get(struct future *f) {
	if (f->threadRunningF == -1) { //submitted by external to global queue
		pthread_mutex_lock(&f->pool->globalQueueLock);
	} else {
		pthread_mutex_lock(&f->pool->threads[f->threadRunningF].lock);
	}
	while (f->futureState != DONE) {
		if (cur_thread != NULL && f->futureState == UNSTARTED) { //Steal it and do it
			list_remove(&f->e);
			f->futureState = WORKING;
			if (f->threadRunningF == -1) { //submitted by external to global queue
				pthread_mutex_unlock(&f->pool->globalQueueLock);
			} else {
				pthread_mutex_unlock(&f->pool->threads[f->threadRunningF].lock);
			}
			run_future(f);
			return f->result;
		}
/*		else if (cur_thread != NULL && f->futureState == WORKING && !list_empty(&f->pool->threads[f->threadRunningF-1].queue)) {
		// Steal a task to help that thread.
		struct thread_struct * vic = &f->pool->threads[f->threadRunningF-1];
		pthread_mutex_lock(&vic-> queueLock);
		pthread_mutex_unlock(&f->futureStateLock);
		struct list_elem *fe = list_pop_back(&vic->queue);
		struct future *vic_fut = list_entry(fe, struct future, e);

		pthread_mutex_unlock(&vic-> queueLock);

		run_future(vic_fut);
		}*/
		else {//Just wait
			while(f->futureState != DONE) {
				pthread_cond_wait(&f->finished_cond, &f->pool->threads[f->threadRunningF].lock);
			}
			return f->result;
		}
	}
	if (f->threadRunningF == -1) { //submitted by external to global queue
		pthread_mutex_unlock(&f->pool->globalQueueLock);
	} else {
		pthread_mutex_unlock(&f->pool->threads[f->threadRunningF].lock);
	}
	return f->result;
}



/* Deallocate this future.  Must be called after future_get() */
void future_free(struct future * fut) {
	free(fut);
}

