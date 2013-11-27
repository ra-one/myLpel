#ifndef _HRC_WORKER_H_
#define _HRC_WORKER_H_

#include <pthread.h>
#include <time.h>
#include <hrc_lpel.h>
#include "lpel_main.h"
#include "arch/mctx.h"
#include "hrc_task.h"
#include "mailbox.h"
#include "hrc_taskqueue.h"
#include "hrc_stream.h"


#define  WORKER_MSG_TERMINATE 	1
#define  WORKER_MSG_WAKEUP			2		// send to both master and wrapper
#define  WORKER_MSG_ASSIGN			3
#define  WORKER_MSG_REQUEST			4		// worker request task
#define  WORKER_MSG_RETURN			5		// worker return tasks
#define  WORKER_MSG_INC_FREQ    6   // call from sosi to change freq

typedef struct timeval timeval_t;

typedef struct workerctx_t {
  int wid;
  pthread_t     thread;
  mctx_t        mctx;
  int           terminate;
  lpel_task_t  *current_task;
  mon_worker_t *mon;
  mailbox_t    *mailbox;
  char          padding[64];
  lpel_stream_t *free_stream;
  lpel_stream_desc_t *free_sd;
  struct workerctx_t *next;		// to organise the list of free wrappers
} workerctx_t;


typedef struct masterctx_t {
  pthread_t     thread;
  mctx_t        mctx;
  int           terminate;
  //mon_worker_t *mon; // FIXME
  mailbox_t    *mailbox;
  taskqueue_t  *ready_tasks;
  taskqueue_t  *ready_wrappers;
  char          padding[64];
  int *waitworkers;
  int first_wait; // index to update wait workers
  int next_wait;  // index to update wait workers
  int num_workers;
  int *waitwrappers;
  int num_wrappers;
  workerctx_t **workers;
  /* info for waiting time monitoring */
  int window_size;
  int wait_threshold;
  double *start_worker_wait; // array of start of waiting time for worker, index is wid
  double *window_wait;
  double *window_start;
  int next_window_index;  // 
  unsigned int count_wait;
} masterctx_t;



workerctx_t *LpelCreateWrapperContext(int wid);		// can be wrapper or source/sink
workerctx_t *LpelWorkerSelf(void);
lpel_task_t *LpelWorkerCurrentTask(void);

void LpelWorkerTaskWakeup( lpel_task_t *whom);
void LpelWorkerTaskExit(lpel_task_t *t);
void LpelWorkerTaskYield(lpel_task_t *t);
void LpelWorkerTaskBlock(lpel_task_t *t);
void LpelWorkerRunTask( lpel_task_t *t);

void LpelWorkerBroadcast(workermsg_t *msg);



/* put and get free stream */
void LpelWorkerPutStream(workerctx_t *wc, lpel_stream_t *s);
lpel_stream_t *LpelWorkerGetStream();

/* put and get free stream desc*/
void LpelWorkerPutSd(workerctx_t *wc, lpel_stream_desc_t *s);
lpel_stream_desc_t *LpelWorkerGetSd(workerctx_t *wc);

/* destroy list of free stream and stream desc when terminate worker */
void LpelWorkerDestroyStream(workerctx_t *wc);
void LpelWorkerDestroySd(workerctx_t *wc);



/****************** WORKER/WRAPPER/MASTER THREAD **********************************/
void *WorkerThread(void *arg);
void *MasterThread(void *arg);
void *WrapperThread(void *arg);

/******************* INI local vars *****************************/
void initLocalVar(int size, int wrappers);
void cleanupLocalVar();
void setupMailbox(mailbox_t **mastermb, mailbox_t **workermbs);

#endif /* _HRC_WORKER_H_ */
