#include <pthread.h>
#include <stdlib.h>
#include "threadpool.h"
#include "list.h"
#include <string.h>
#include <stdio.h>

static __thread int internalExternal;
//static pthread_mutex_t taskLock;
//static pthread_mutex_t threadLock;


static void * thread_runner(void *);
struct thread_pool {
    pthread_mutex_t globalQueueLock;
    pthread_mutex_t shutDown_Lock;

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
    //pthread_mutex_lock(&f->futureStateLock);
    //f->futureState = WORKING;
    //pthread_cond_signal(&future_cond);
    //pthread_mutex_unlock(&f->futureStateLock);

    f->result = f->task(f->pool, f->data);

    pthread_mutex_lock(&f->futureStateLock);

    f->futureState = DONE;
    //printf("future done so signaling\n");
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
	internalExternal = 0;
	//futureDone = false;
	pthread_mutex_init(&pool->globalQueueLock, NULL);

    pthread_mutex_init(&pool->shutDown_Lock, NULL);
    //pthread_mutex_init(&threadLock, NULL);
   // pthread_mutex_init(&taskLock, NULL);
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
    struct thread_struct * thread = (struct thread_struct *) t;
    //pthread_mutex_lock(&threadLock);
    //printf("Changing internal external value to %d\n", thread->threadNum);
    internalExternal = thread->threadNum;
    //pthread_mutex_unlock(&threadLock);
    //printf("internal external value = %d\n", internalExternal);
    pthread_mutex_lock(&thread->pool->shutDown_Lock);

    while(!thread->pool->shutDown){

        pthread_mutex_unlock(&thread->pool->shutDown_Lock);

        pthread_mutex_lock(&thread->queueLock);

        if(!list_empty(&thread->queue)) {
            pthread_mutex_lock(&list_entry(list_begin(&thread->queue),struct future, e)->futureStateLock);
            struct list_elem *elem = list_pop_front(&thread->queue);
            struct future *f = list_entry(elem, struct future, e);

            f->futureState = WORKING;
            pthread_mutex_unlock(&f->futureStateLock);
            pthread_mutex_unlock(&thread->queueLock);
            run_future(f);
        }
        else  {
            //pthread_mutex_unlock(&list_entry(list_begin(&thread->queue),struct future, e)->futureStateLock);
            pthread_mutex_unlock(&thread->queueLock);
            //printf("other thread runni\n");
            pthread_mutex_lock(&thread->pool->globalQueueLock);

            // Take from global queue
            if(!list_empty(&thread->pool->globalQueue)){
                pthread_mutex_lock(&list_entry(list_begin(&thread->pool->globalQueue),struct future, e)->futureStateLock);
                struct list_elem *elem = list_pop_front(&thread->pool->globalQueue);
                struct future *f = list_entry(elem, struct future, e);

                f->futureState = WORKING;
                //pthread_cond_signal(&future_cond);

                pthread_mutex_unlock(&f->futureStateLock);
                pthread_mutex_unlock(&thread->pool->globalQueueLock);
                run_future(f);
                //f->result = f->task(f->pool, f->data);

            }
            else {
                //pthread_mutex_unlock(&list_entry(list_rbegin(&thread->pool->globalQueue),struct future, e)->futureStateLock);
                pthread_mutex_unlock(&thread->pool->globalQueueLock);
                /**Stealing Implementation*/
                for(int i = thread->threadNum-1; i >= 0; i--) {

                    struct thread_struct * vic = &thread->pool->threads[i];
                    pthread_mutex_lock(&vic->queueLock);
                    if(!list_empty(&vic->queue)) {
                        pthread_mutex_lock(&list_entry(list_rbegin(&vic->queue),struct future, e)->futureStateLock);
                        struct list_elem *elem = list_pop_back(&vic->queue);
                        struct future *f = list_entry(elem, struct future, e);

                        f->futureState = WORKING;
                        //pthread_cond_signal(&future_cond);

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
    //printf("%d releasing local, global, shutdown lock\n", internalExternal);
   // pthread_mutex_unlock(&thread->pool->globalQueueLock);
    //pthread_mutex_unlock(&thread->queueLock);
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
//also destroy mutex lock
    //pthread_mutex_lock(&queueLock);
    //printf("%d acquiring shutdown lock\n", internalExternal);
    pthread_mutex_lock(&pool->shutDown_Lock);
    pool->shutDown = true;
    pthread_mutex_unlock(&pool->shutDown_Lock);
    //printf("%d released shutdown lock\n", internalExternal);
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
            //printf("internal external value inside submit = %d\n", internalExternal);
            struct future *f= malloc(sizeof(struct future));
            f->task = task;
            f->pool = pool;
            f->data = data;
            f->futureState = UNSTARTED;
            f->result = NULL;
            f->threadRunningF = internalExternal;
            pthread_mutex_init(&f->futureStateLock, NULL);
            pthread_cond_init(&f->future_cond, NULL);
            if(internalExternal == 0) {
                //external submission
                //printf("acquiring global queue lock to push\n");
                pthread_mutex_lock(&pool->globalQueueLock);
                //printf("acquired global queue lock to push\n");
               // printf("%d pushing to global Queue\n", internalExternal);
                list_push_front(&pool->globalQueue, &f->e);
                //printf("%d Unlocking global queue lock after pushing\n", internalExternal);
                pthread_mutex_unlock(&pool->globalQueueLock);
                //printf("global queue lock released\n");

            }
            else {
                //printf("acquiring local queue lock %d to push\n", internalExternal);
                pthread_mutex_lock(&pool->threads[internalExternal-1].queueLock);
                //printf("pushing to local Queue %d\n", internalExternal);
                list_push_front(&pool->threads[internalExternal-1].queue, &f->e);
                //printf("%d Unlocking local queue lock after pushing\n", internalExternal);
                pthread_mutex_unlock(&pool->threads[internalExternal-1].queueLock);
                //printf("local queue lock %d released\n", internalExternal);
                //internal submission
                //pthread_t thread;
                //pthread_create(&thread, NULL, thread_runner, (void *) thread_struct);
                //list_remove(&f->e);
                //printf("Unlocking queue lock\n");
                //pthread_mutex_unlock(&queueLock);
                //run_future(f);



            }
            return f;

}

/* Make sure that the thread pool has completed the execution
 * of the fork join task this future represents.
 *
 * Returns the value returned by this task.
 */
void * future_get(struct future *f) {

    	if(f->threadRunningF == 0) {
            	//printf("It's on the global queue and %d is trying to get it\n", internalExternal);
        	//printf("trying to lock global queue because i'm about to get from it\n");
        	pthread_mutex_lock(&f->pool->globalQueueLock);
        	//printf("locked global queue because i'm about to get from it\n");
    	}
    	else {
		//printf("trying to lock local queue %d because i'm about to get from it\n", internalExternal);
		pthread_mutex_lock(&f->pool->threads[f->threadRunningF-1].queueLock);
		//printf("locked local queue %d because i'm about to get from it\n", internalExternal);
	}


	pthread_mutex_lock(&f->futureStateLock);
	if(f->futureState == DONE) {
        	pthread_mutex_unlock(&f->futureStateLock);
        	if(f->threadRunningF == 0) {
            		pthread_mutex_unlock(&f->pool->globalQueueLock);
        	}
        	else {
            		pthread_mutex_unlock(&f->pool->threads[f->threadRunningF-1].queueLock);
        	}

        	//pthread_mutex_unlock(&f->pool->globalQueueLock);
        	return f->result;
	}
	else if(f->futureState == UNSTARTED){
        	list_remove(&f->e);
        	f->futureState = WORKING;
        	int temp = f->threadRunningF;
        	f->threadRunningF = internalExternal;
        	pthread_mutex_unlock(&f->futureStateLock);
        	if(temp == 0) {
            		pthread_mutex_unlock(&f->pool->globalQueueLock);
        	}
        	else {
            		pthread_mutex_unlock(&f->pool->threads[temp-1].queueLock);
        	}
        	run_future(f);
        	return f->result;
	}
	else { // Helping code
	    	if(f->threadRunningF == 0) {
            		pthread_mutex_unlock(&f->pool->globalQueueLock);
			while (f->futureState != DONE) {
				pthread_cond_wait(&f->future_cond, &f->futureStateLock);
			}
        	}
       		else {
            		//pthread_mutex_unlock(&f->pool->threads[f->threadRunningF-1].queueLock);
			struct thread_struct * vic = &f->pool->threads[f->threadRunningF-1];
			if (!list_empty(&vic->queue)) {
				pthread_mutex_lock(&list_entry(list_rbegin(&vic->queue), struct future, e)->futureStateLock);
				struct future *vic_fut = list_entry(list_pop_back(&vic->queue), struct future, e);
				vic_fut->futureState = WORKING;
				pthread_mutex_unlock(&vic_fut->futureStateLock);
				pthread_mutex_unlock(&vic->queueLock);
				run_future(vic_fut);
			}
			else {
				pthread_mutex_unlock(&vic->queueLock);
				while (f->futureState != DONE) {
					pthread_cond_wait(&f->future_cond, &f->futureStateLock);
				}
        		}
		}
        	/*while(f->futureState != DONE) {
            		pthread_cond_wait(&f->future_cond, &f->futureStateLock);
        	}*/
		//struct thread_struct * vic = &f->pool->threads[f->threadRunningF-1];
		//pthread_mutex_lock(&vic->queueLock);
		/*if (!list_empty(&vic->queue)) {
			pthread_mutex_lock(&list_entry(list_rbegin(&vic->queue), struct future, e)->futureStateLock);
			struct future *vic_fut = list_entry(list_pop_back(&vic->queue), struct future, e);
			vic_fut->futureState = WORKING;
			pthread_mutex_unlock(&vic_fut->futureStateLock);
			pthread_mutex_unlock(&vic->queueLock);
			run_future(vic_fut);
		}
		else {
			pthread_mutex_unlock(&vic->queueLock);
		}*/
        	pthread_mutex_unlock(&f->futureStateLock);

        	return f->result;
	}

}



/* Deallocate this future.  Must be called after future_get() */
void future_free(struct future * fut) {

	free(fut);
}

