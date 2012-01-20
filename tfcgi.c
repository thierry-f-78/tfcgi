#include <errno.h>
#define NO_FCGI_DEFINES
#include <fcgi_stdio.h>
#ifdef USE_THREADS
#include <pthread.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "tfcgi.h"

#define STACK_SIZE (256*1024)

struct tfcgi {
	FCGX_Request fcgi;   /* fcgi pointer */
	struct tfcgi *next;  /* for chain into different pool */
};

/* pools */
struct tfcgi *run_queue  = NULL;
struct tfcgi *free_queue = NULL;

#ifdef USE_THREADS

/* mutex for the pools */
pthread_mutex_t start_ctl        = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t read_run_queue   = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t read_free_queue  = PTHREAD_MUTEX_INITIALIZER;

/* conditions for finish empty */
pthread_cond_t c_start_ctl       = PTHREAD_COND_INITIALIZER;
pthread_cond_t c_run_queue       = PTHREAD_COND_INITIALIZER;
pthread_cond_t c_free_queue      = PTHREAD_COND_INITIALIZER;

/* array of threads */
pthread_t *all_threads;

#endif

/* user params */
void (*tfcgi_init_threads)(int thread_id)            = NULL;
void (*tfcgi_exec)(FCGX_Request fcgi, int thread_id) = NULL;
/* NULL listen on socket 0, this initialized by the launcher */
char *tfcgi_socket                                   = NULL;
int tfcgi_backlog                                    = 50;
mode_t tfcgi_socket_mode                             = 0;
int tfcgi_threads_nb                                 = 10;

#define LOGMSG(lvl, fmt, args...) \
        fprintf(stderr, fmt, ## args);

void *thread_start(void *parg) {
	struct tfcgi *rq;
	long thread_id;

	// get thread id
	thread_id = (long)parg;

	// execute user code init thread
	if (tfcgi_init_threads != NULL) {
		tfcgi_init_threads(thread_id);
	}

#ifdef USE_THREADS

	// lock read_run_queue.
	pthread_mutex_lock(&read_run_queue);

	// try to lock start_ctl. the main function hold this mutex
	// the main function start a waiting signal for this mutex
	pthread_mutex_lock(&start_ctl);

	// send signal at the main function
	pthread_cond_signal(&c_start_ctl);
	pthread_mutex_unlock(&start_ctl);

	// waiting for a job
	LOGMSG(LOG_DEBUG, "[thread %lu] wait for a job in run_queue",
	             thread_id);
	pthread_cond_wait(&c_run_queue, &read_run_queue);

	// main loop
	while(1) {

#endif

		/* here the mutex read_run_queue is locked */
		rq = run_queue;
		run_queue = rq->next;

#ifdef USE_THREADS

		/* unlock free queue */
		pthread_mutex_unlock(&read_run_queue);

#endif

		/* exec code page */
		if (tfcgi_exec != NULL)
			tfcgi_exec(rq->fcgi, thread_id);

		/* end of fcgi traitment */
		FCGX_Finish_r(&rq->fcgi);

#ifdef USE_THREADS

		/* lock free_queue for add finished job into */
		pthread_mutex_lock(&read_free_queue);

		/* lock the run_queue for make sure that no job are added
		 * until the cond_wait function on mutex read_run_queue
		 */
		pthread_mutex_lock(&read_run_queue);

		/* set datas in free_queue */
		rq->next = free_queue;
		free_queue = rq;

		/* send signal for a new job released in free_queue */
		LOGMSG(LOG_DEBUG, "[thread %lu] send finish signal",
		             thread_id);
		pthread_mutex_unlock(&read_free_queue);
		pthread_cond_signal(&c_free_queue);

		/* wait for a new job signal */
		LOGMSG(LOG_DEBUG,
		             "[thread %lu] wait for a job in run_queue", thread_id);
		pthread_cond_wait(&c_run_queue, &read_run_queue);
	}

#else

	rq->next = free_queue;
	free_queue = rq;

#endif

	return NULL;
}

void tfcgi_start(void) {
	int ret_code;
	struct stat statbuf;
	int listen_socket;
	struct tfcgi *rq;
#ifdef USE_THREADS
	long i;
	pthread_attr_t tattr;
#endif

	/* check params */
	if (tfcgi_exec == NULL) {
		LOGMSG(LOG_CRIT, "No function defined for main appli, use \"tfcgi_set_exec\"");
	}

	/* init fcgi system */
	if(FCGX_Init()){
		LOGMSG(LOG_CRIT, "FCGX_Init(): error");
		exit(1);
	}

	/* network bind */
	listen_socket = FCGX_OpenSocket(tfcgi_socket, tfcgi_backlog);
	if (listen_socket < 0) {
		LOGMSG(LOG_ERR,
		       "FCGX_OpenSocket(%s, %d): error",
		       tfcgi_socket, tfcgi_backlog);
		exit(1);
	}

	/* set rights to socket file */
	ret_code = stat(tfcgi_socket, &statbuf);
	if(ret_code == 0 && S_ISSOCK(statbuf.st_mode)){
		ret_code = chmod(tfcgi_socket, tfcgi_socket_mode);
		if (ret_code == -1) {
			LOGMSG(LOG_CRIT, "chmod(%s, %d)[%d]: %s",
			       tfcgi_socket, tfcgi_socket_mode, errno, strerror(errno));
			exit(1);
		}
	}

#ifdef USE_THREADS

	/* alloc memory for all threads */
	all_threads = (pthread_t *)calloc(tfcgi_threads_nb,
	                                  sizeof(pthread_t));
	if (all_threads == NULL) {
		LOGMSG(LOG_ERR, "malloc: %s", strerror(errno));
		exit(1);
	}

	/* init all threads */
	for (i=0; i<tfcgi_threads_nb; i++) {

#endif

		/* allocate memory for threads */
		rq = (struct tfcgi *)malloc(sizeof(struct tfcgi));
		if (rq == NULL) {
			LOGMSG(LOG_ERR, "malloc: %s", strerror(errno));
			exit(1);
		}

		/* chain it */
		rq->next = free_queue;
		free_queue = rq;

		/* init FastCGI request */
		if(FCGX_InitRequest(&rq->fcgi, listen_socket, 0)){
			LOGMSG(LOG_ERR, "FCGX_InitRequest(): error");
			exit(1);
		}

#ifdef USE_THREADS

		/* lock starting mutex */
		pthread_mutex_lock(&start_ctl);

		/* set stacksize */
		pthread_attr_init(&tattr);
		pthread_attr_setstacksize(&tattr, STACK_SIZE);

		/* launch thread
		 * the thread lock(read_run_queue) and try to lock(start_ctl)
		 */
		ret_code = pthread_create(&(all_threads[i]), &tattr,
		                          thread_start, (void *)i);
		if (ret_code != 0) {
			switch (ret_code) {
				case EAGAIN:
					LOGMSG(LOG_ERR,
					             "The system lacked the necessary resources "
					             "to create another thread, or the "
					             "system-imposed limit on the total number of "
					             "threads in a process PTHREAD_THREADS_MAX "
					             "would be exceeded.");
					break;

				case EINVAL:
					LOGMSG(LOG_ERR,
					             "The value specified by attr is invalid.");
					break;

				case EPERM:
					LOGMSG(LOG_ERR,
					             "The caller does not have appropriate "
					             "permission to set the required scheduling "
					             "parameters or scheduling policy.");
					break;
			}
			exit(1);
		}

		/* wait for thread starting
		 * unlock start_ctl, this is now locked by the thread
		 * the thread send a signal, and un lock start_ctl
		 */
		pthread_cond_wait(&c_start_ctl, &start_ctl);

		/* try to lock read_run_queue. the thread must wait
		 * for a signal on mutex read_run_queue.
		 */
		pthread_mutex_lock(&read_run_queue);

		/* the thread is currently waiting for a read_run_queue signal.
		 * un lock all
		 */
		pthread_mutex_unlock(&read_run_queue);
		pthread_mutex_unlock(&start_ctl);
		LOGMSG(LOG_DEBUG, "thread %lu started", i);
	}

#endif

	/* main loop */
	while(1) {

#ifdef USE_THREADS

		/* lock free queue */
		pthread_mutex_lock(&read_free_queue);

		/* if finish null, wait for read_free_queue signal */
		while (free_queue == NULL) {
			LOGMSG(LOG_DEBUG, "[thread main] attente d'un slot libre");
			pthread_cond_wait(&c_free_queue, &read_free_queue);
		}

		/* get next free element */
		rq = free_queue;
		free_queue = rq->next;

		/* unlock free queue */
		pthread_mutex_unlock(&read_free_queue);

#else

		/* get the only one avalaible in free queue */
		rq = free_queue;
		free_queue = rq->next;

#endif


		/* accept new connexion */
		FCGX_Accept_r(&rq->fcgi);



#ifdef USE_THREADS

		/* lock run queue */
		pthread_mutex_lock(&read_run_queue);

		/* add connexion in run queue */
		rq->next = run_queue;
		run_queue = rq;

		/* unlock run queue */
		LOGMSG(LOG_DEBUG, "[thread main] send start signal");
		pthread_mutex_unlock(&read_run_queue);
		pthread_cond_signal(&c_run_queue);

#else

		/* set the only one avalaible in run queue */
		rq->next = run_queue;
		run_queue = rq;
		thread_start(0);

#endif

	}
}

