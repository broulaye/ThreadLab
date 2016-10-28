#include <pthread.h>

struct threadpool {
	struct thread_struct * threads;
	int nthreads;
};
struct future {
	struct threadpool *pool;
	fork_join_task_t task;
	void *data;
};
struct thread_struct {
	pthread_t thread;
};

void * thread_runner() {

}

/* Create a new thread pool with no more than n threads. */
struct thread_pool * thread_pool_new(int nthreads) {
	struct threadpool *pool = malloc (sizeof(struct threadpool));
	pool->threads = malloc(nthreads * sizeof(struct thread_struct));
	pool->nthreads = nthreads;
	int rc;
	pthread_attr_t attr;
	pthrad_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	for (int i = 0; i < nthreads; i++) {
		rc = pthread_create(&threads[i].thread, &attr, thread_runner, NULL); // Params will need to be changed
		// add mutex lock to thread_struct, init it, etc.
		if (rc) {
			printf("ERROR: return code from pthread_create() is %d\n", rc);
			exit(-1);
		}
	}
	pthread_attr_destroy(&attr);
	return pool;
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
	int rc;
	for (int i = 0; i < pool->nthreads; i++) {
		rc = pthread_join(pool->threads[i].thread, NULL);
		if (rc) {
			printf("ERROR: return code from pthread_join() is %d\n", rc);
			exit(-1);
		}
	}


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

}

/* Make sure that the thread pool has completed the execution
 * of the fork join task this future represents.
 *
 * Returns the value returned by this task.
 */
void * future_get(struct future *) {

}

/* Deallocate this future.  Must be called after future_get() */
void future_free(struct future * fut) {
	free(fut->data);
	free(fut);
}

