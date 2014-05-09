/*
 * hrc_worker_op.c
 *
 *  Created on: 17 Jul 2013
 *      Author: administrator
 */

/*
 * The LPEL worker containing code for workers, master and wrappers
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>
#include <float.h>
#include <sys/time.h>

#include <pthread.h>
#include "arch/mctx.h"

#include "arch/atomic.h"

#include "hrc_worker.h"
#include "hrc_task.h"
#include "lpel_hwloc.h"
#include "lpelcfg.h"
#include "hrc_stream.h"
#include "mailbox.h"
#include "lpel/monitor.h"
#include "lpel_main.h"

#include "scc_lpel.h"

//#define _USE_WORKER_DBG__

#define MASTER_DBG //

#ifdef _USE_WORKER_DBG__
#define WORKER_DBG printf
#else
#define WORKER_DBG	//
#endif

#ifdef USE_SCC
#define LpelThreadAssign //
#endif

/******************* PRIVATE FUNCTIONS *****************************/
static void addFreeWrapper(workerctx_t *wp);
static workerctx_t *getFreeWrapper();

/******************************************************************************/
static int num_workers = -1;
static int num_wrappers = -1;
static mailbox_t *mastermb;
static mailbox_t **workermbs;

static workerctx_t *freewrappers;
static PRODLOCK_TYPE lockwrappers;

#ifdef HAVE___THREAD
static TLSSPEC workerctx_t *workerctx_cur;
#else /* HAVE___THREAD */
static pthread_key_t workerctx_key;
#endif /* HAVE___THREAD */
/******************************************************************************/

void initLocalVar(int size, int wrappers){
#ifndef HAVE___THREAD
	/* init key for thread specific data */
	pthread_key_create(&workerctx_key, NULL);
#endif /* HAVE___THREAD */

  /* free wrappers */
  freewrappers = NULL;
	PRODLOCK_INIT(&lockwrappers);
  num_workers = size; //add +1 for master
  num_wrappers = wrappers;
  /* mailboxes */
  workermbs = (mailbox_t **) malloc(sizeof(mailbox_t *) * (num_workers+num_wrappers));
  setupMailbox(&mastermb, workermbs);
}

void cleanupLocalVar(){
#ifndef HAVE___THREAD
	pthread_key_delete(workerctx_key);
#endif /* HAVE___THREAD */

  /* free wrappers */
	workerctx_t *wp = freewrappers;
	workerctx_t *next;
	while (wp != NULL) {
		next = wp->next;
		LpelMailboxDestroy(wp->mailbox);
		LpelWorkerDestroyStream(wp);
		LpelWorkerDestroySd(wp);
		SCCFreePtr(wp);
		wp = next;
	}
	PRODLOCK_DESTROY(&lockwrappers);
  
  /* mailboxes */
  free(workermbs);
}


/**
 * Assign a task to the worker by sending an assign message to that worker
 */
void LpelWorkerRunTask(lpel_task_t *t) {
	 workermsg_t msg;
   msg.type = WORKER_MSG_ASSIGN;
	 msg.body.task = t;
   //printf("workerOP: task %p id %d state %c sent to master\n",t,t->uid,t->state);
	 LpelMailboxSend(mastermb, &msg);

}


static void returnTask(lpel_task_t *t) {
	workermsg_t msg;
	msg.type = WORKER_MSG_RETURN;
	msg.body.task = t;
	LpelMailboxSend(mastermb, &msg);
}


static void requestTask(workerctx_t *wc) {
	WORKER_DBG("worker %d: request task\n", wc->wid);
	workermsg_t msg;
	msg.type = WORKER_MSG_REQUEST;
	msg.body.from_worker = wc->wid;
	LpelMailboxSend(mastermb, &msg);
#ifdef USE_LOGGING
	if (wc->mon && MON_CB(worker_waitstart)) {
		MON_CB(worker_waitstart)(wc->mon);
	}
#endif
}


static void sendTask(int wid, lpel_task_t *t) {
	assert(t->state == TASK_READY);
	workermsg_t msg;
	msg.type = WORKER_MSG_ASSIGN;
	msg.body.task = t;
	LpelMailboxSend(workermbs[wid], &msg);
 }

static void sendWakeup(mailbox_t *mb, lpel_task_t *t)
{
  workermsg_t msg;
	msg.type = WORKER_MSG_WAKEUP;
	msg.body.task = t;
	LpelMailboxSend(mb, &msg);
}

/*******************************************************************************
 * MASTER FUNCTION
 ******************************************************************************/
// only called by sosi
void increaseFrequency(){
  //printf("ah ha!! change cpu frequency please %f\n",SCCGetTime());
  workermsg_t msg;
  msg.type = WORKER_MSG_INC_FREQ;
  LpelMailboxSend(mastermb, &msg);
}

//only called by master
void decreaseFrequency(){
  printf("Master: decrease frequency %f\n",SCCGetTime());
  change_freq(0);
}

/* evaluate waiting time of all workers, consider decrease cpu frequency */
static void evaluateWaiting(masterctx_t *master, double cur) {
  // if wait_threshold is < 0 then do not change freq
  if (master->wait_threshold < 0) return; 
  
  master->count_wait++;
  
  // skip when waiting window is not full
  if (master->count_wait < master->window_size) return;

  int i;
  double wait = 0.0;
  for (i = 0; i < master->window_size; i++) {
    wait = wait + master->window_wait[i];
  }
  int first = master->next_window_index; // when the window is full, the next index in the window is the first waiting time, i.e. the one will be overwritten next
  double observe_time = ((cur*1000000)-(master->window_start[first]*1000000));
  double prop_wait = wait*100.0/(master->num_workers * observe_time);
  //printf("prop wait %f, %f\n", prop_wait, observe_time);
  if (prop_wait > master->wait_threshold) {
    master->count_wait = 0;
    //printf("reduce frequency\n");
    decreaseFrequency();
  }
}

/* add a worker to the waiting list */
static void addWait(masterctx_t *master, int worker) {
  if (master->first_wait == master->next_wait) { //array of waiting worker must be either empty or full
    assert(master->waitworkers[master->first_wait] == -1); // can not be full
  }
  master->waitworkers[master->next_wait] = worker;
  master->next_wait = (master->next_wait + 1) % master->num_workers;
  // get time when it start waiting
  master->start_worker_wait[worker] = SCCGetTime();
}

/* get the first waiting worker, if non, return -1 */
static int getWait(masterctx_t *master) {
  if (master->first_wait == master->next_wait) { //array of waiting worker must be either empty or full
     // empty
    if (master->waitworkers[master->first_wait] == -1) return -1;
  }

  int worker = master->waitworkers[master->first_wait];
  master->waitworkers[master->first_wait] = -1;
  master->first_wait = (master->first_wait + 1) % master->num_workers;

  // get time when it stop waiting
  double cur_time = SCCGetTime();
  
  double wait = ((cur_time*1000000)-(master->start_worker_wait[worker]*1000000));
  master->window_wait[master->next_window_index] = wait;
  master->window_start[master->next_window_index] = master->start_worker_wait[worker]; /* only update when accounting new waiting period */
  master->next_window_index = (master->next_window_index + 1) % master->window_size;
  
  evaluateWaiting(master, cur_time);

  return worker;
}

static int servePendingReq(masterctx_t *master, lpel_task_t *t) {
  int w;
  t->sched_info.prior = LpelTaskCalPriority(t);
  w = getWait(master);
  if (w != -1) {
    WORKER_DBG("master: send task %d to waiting worker %d\n", t->uid, w);
    sendTask(w, t);
  }
  return w;
}

static int servePendingWrap(masterctx_t *master, lpel_task_t *t) {
	int i;
	for (i = 0; i < num_wrappers; i++){
		if (master->waitwrappers[i] == 1) {
			master->waitwrappers[i] = 0;
			WORKER_DBG("master: send task %d to wrapper %d\n", t->uid, i);
			sendTask(i, t);
			return i;
		}
	}
	return -1;
}

static void updatePriorityList(taskqueue_t *tq, stream_elem_t *list, char mode) {
	double np;
	lpel_task_t *t;
	lpel_stream_t *s;
	while (list != NULL) {
		s = list->stream_desc->stream;
		if (mode == 'r')
			t = LpelStreamProducer(s);
		else if (mode == 'w')
			t = LpelStreamConsumer(s);
		if (t && t->state == TASK_INQUEUE) {
			np = LpelTaskCalPriority(t);
			LpelTaskqueueUpdatePriority(tq, t, np);
		}
		list = list->next;
	}
}

/* update prior for neighbors of t
 * @cond: called only by master to avoid concurrent access
 */
static void updatePriorityNeigh(taskqueue_t *tq, lpel_task_t *t) {
	updatePriorityList(tq, t->sched_info.in_streams, 'r');
	updatePriorityList(tq, t->sched_info.out_streams, 'w');
}


static void MasterLoop(masterctx_t *master)
{
  FILE *waitingTaskLogFile = fopen("/shared/nil/Out/waitingTask.log", "w");
  
	MASTER_DBG("start master\n");
	do {
		workermsg_t msg;

		LpelMailboxRecv(mastermb, &msg);
    MASTER_DBG("\n\n\nmaster: MSG received, handle it! %f\n",SCCGetTime());
		lpel_task_t *t;
		int wid;
		switch(msg.type) {
		case WORKER_MSG_ASSIGN:
			/* master receive a new task */
			t = msg.body.task;
      //printf("master: task %p id %d state %c\n",t,t->uid,t->state);
			assert (t->state == TASK_CREATED);
			t->state = TASK_READY;
      if(t->wrapper == 0){ //normal task
        MASTER_DBG("master: got normal task %d\n", t->uid);
        if (servePendingReq(master, t) < 0) {		 // no pending request
          t->sched_info.prior = DBL_MAX; //created task does not set up input/output stream yet, set as highest priority
          t->state = TASK_INQUEUE;
          LpelTaskqueuePush(master->ready_tasks, t);
        }
      } else { //wrapper task
        MASTER_DBG("master: got wrapper task %d\n", t->uid);
        if (servePendingWrap(master, t) < 0) {		 // no pending request
          t->state = TASK_INQUEUE;
          LpelTaskqueuePush(master->ready_wrappers, t);
        }
      }
			break;

		case WORKER_MSG_RETURN:
			t = msg.body.task;
			MASTER_DBG("master: worker returned task %d\n", t->uid);
			switch(t->state) {
			case TASK_BLOCKED:
				if (t->wakenup == 1) {	/* task has been waked up */
          MASTER_DBG("task %d was put in ready que\n",t->uid);
					t->wakenup = 0;
					t->state = TASK_READY;
					// no break, task will be treated as if it is returned as ready
				} else {
          MASTER_DBG("task %d state was changed to returned\n",t->uid);
					t->state = TASK_RETURNED;
					updatePriorityNeigh(master->ready_tasks, t);
					break;
				}

			case TASK_READY:	// task yields
      MASTER_DBG("master: task %d added to ready que\n",t->uid);
#ifdef _USE_NEG_DEMAND_LIMIT_
				t->sched_info.prior = LpelTaskCalPriority(t);
				if (t->sched_info.prior == LPEL_DBL_MIN) {		// if not schedule task if it has too low priority
					t->state = TASK_INQUEUE;
					LpelTaskqueuePush(master->ready_tasks, t);
					break;
				}
#endif
				if (servePendingReq(master, t) < 0) {		// no pending request
					updatePriorityNeigh(master->ready_tasks, t);
					t->sched_info.prior = LpelTaskCalPriority(t);	//update new prior before add to the queue
					t->state = TASK_INQUEUE;
					LpelTaskqueuePush(master->ready_tasks, t);
				}
				break;

			case TASK_ZOMBIE:
        MASTER_DBG("master: zombie task\n");
				updatePriorityNeigh(master->ready_tasks, t);
				LpelTaskDestroy(t);
				break;
			default:
				assert(0);
				break;
			}
			break;

		case WORKER_MSG_WAKEUP:
			t = msg.body.task;
			if (t->state != TASK_RETURNED) {		// task has not been returned yet
        MASTER_DBG("master: put message back, task %d not returned yet\n",t->uid);
				t->wakenup = 1;		// set task as wakenup so that when returned it will be treated as ready
				break;
			}
			MASTER_DBG("master: unblock task %d\n", t->uid);
			t->state = TASK_READY;

#ifdef _USE_NEG_DEMAND_LIMIT_
				t->sched_info.prior = LpelTaskCalPriority(t);
				if (t->sched_info.prior == LPEL_DBL_MIN) {		// if not schedule task if it has too low priority
					t->state = TASK_INQUEUE;
					LpelTaskqueuePush(master->ready_tasks, t);
					break;
				}
#endif

			if (servePendingReq(master, t) < 0) {		// no pending request
#ifndef _USE_NEG_DEMAND_LIMIT_
					t->sched_info.prior = LpelTaskCalPriority(t);	//update new prior before add to the queue
#endif
					t->state = TASK_INQUEUE;
					LpelTaskqueuePush(master->ready_tasks, t);
			}
			break;


		case WORKER_MSG_REQUEST:
			wid = msg.body.from_worker; 
      if (wid < 0){
        MASTER_DBG("master: task request from wrapper %d\n", wid);
        wid *= -1;
        t = LpelTaskqueuePeek(master->ready_wrappers);
        if (t == NULL) {
          master->waitwrappers[wid-(num_workers+1)] = 1;
          MASTER_DBG("master: wrapper %d put into wait wrapper que\n", wid);
        } else {
          t->state = TASK_READY;
          MASTER_DBG("master: task %d sent to wrapper %d\n",t->uid, wid);
          sendTask(wid, t);
          t = LpelTaskqueuePop(master->ready_wrappers);
        }
      } else {
        MASTER_DBG("master: task request from worker %d\n", wid);
        t = LpelTaskqueuePeek(master->ready_tasks);
        if (t == NULL) {
          addWait(master, wid);
          MASTER_DBG("master: worker %d put into wait worker que\n", wid);
        } else {

#ifdef _USE_NEG_DEMAND_LIMIT_
          if (t->sched_info.prior == LPEL_DBL_MIN) {		// if not schedule task if it has too low priority
            addWait(master, wid);
            break;
          }
#endif
          t->state = TASK_READY;
          MASTER_DBG("master: task %d sent to worker %d\n",t->uid, wid);
          sendTask(wid, t);
          t = LpelTaskqueuePop(master->ready_tasks);
        }
      }
			break;

		case WORKER_MSG_TERMINATE:
      MASTER_DBG("master: Termination message\n");
			master->terminate = 1;
			break;
 
    case WORKER_MSG_INC_FREQ:
      printf("Master: increase frequency %f\n",SCCGetTime());
      change_freq(1);
      break;

		default:
			assert(0);
		}
    MASTER_DBG("TaskqueueSize %d, WrapperqueueSize %d\n\n",LpelTaskqueueSize(master->ready_tasks),LpelTaskqueueSize(master->ready_wrappers));
    printf("TaskqueueSize %d\n",LpelTaskqueueSize(master->ready_tasks));
    fprintf(waitingTaskLogFile,"%d\n",LpelTaskqueueSize(master->ready_tasks));
	} while (!(master->terminate && LpelTaskqueueSize(master->ready_tasks) == 0));
  fclose(waitingTaskLogFile);
}



void *MasterThread(void *arg)
{
  masterctx_t *master = (masterctx_t *)arg;
  num_workers = master->num_workers;

//FIXME
#ifdef USE_MCTX_PCL
  assert(0 == co_thread_init());
  master->mctx = co_current();
#endif


  /* assign to cores */
  master->terminate = 0;
  LpelThreadAssign(LPEL_MAP_MASTER);

  // master loop, no monitor for master
  MasterLoop(master);
  // master terminated, now terminate worker
  workermsg_t msg;
  msg.type = WORKER_MSG_TERMINATE;
  LpelWorkerBroadcast(&msg);
  
#ifdef USE_MCTX_PCL
  co_thread_cleanup();
#endif

  return NULL;
}

/*
 * Terminate master and workers
 */
void LpelWorkersTerminate(void) {
	workermsg_t msg;
	msg.type = WORKER_MSG_TERMINATE;
	LpelMailboxSend(mastermb, &msg);
}

/*******************************************************************************
 * WRAPPER FUNCTION
 ******************************************************************************/
static void WrapperLoop(workerctx_t *wp)
{
	lpel_task_t *t = NULL;
	workermsg_t msg;
  
  WORKER_DBG("wrapper: Request work for first time\n");
  requestTask(wp);
  goto FirstTask;
  
	do {
		t = wp->current_task;
		if (t != NULL) {
			/* execute task */
			WORKER_DBG("wrapper: switch to task %d tctx %p from wctx %p\n", t->uid,&t->mctx,&wp->mctx);
			assert(t->worker_context == wp);
      mctx_switch(&wp->mctx, &t->mctx);
		} else {
FirstTask:
			/* no ready tasks */
			LpelMailboxRecv(wp->mailbox, &msg);
      WORKER_DBG("\n\n\nwrapper: MSG received, handle it! %f\n",SCCGetTime());
			switch(msg.type) {
			case WORKER_MSG_ASSIGN:
				t = msg.body.task;
				WORKER_DBG("wrapper: got task %d\n", t->uid);
				t->state = TASK_READY;
				wp->current_task = t;
				t->worker_context = wp;
#ifdef USE_LOGGING
				if (t->mon) {
					if (MON_CB(worker_create_wrapper)) {
						wp->mon = MON_CB(worker_create_wrapper)(t->mon);
					} else {
						wp->mon = NULL;
					}
				}
				if (t->mon && MON_CB(task_assign)) {
					MON_CB(task_assign)(t->mon, wp->mon);
				}
#endif
				break;

			case WORKER_MSG_WAKEUP:
				t = msg.body.task;
				WORKER_DBG("wrapper: unblock task %d\n", t->uid);
				assert (t->state == TASK_BLOCKED);
				t->state = TASK_READY;
				assert(t->worker_context == wp);
				wp->current_task = t;
#ifdef USE_LOGGING
				if (t->mon && MON_CB(task_assign)) {
					MON_CB(task_assign)(t->mon, wp->mon);
				}
#endif
				break;
        
			default:
				assert(0);
				break;
			}
		}
	} while (!wp->terminate);
	LpelTaskDestroy(wp->current_task);
	/* cleanup task context marked for deletion */
}

void *WrapperThread(void *arg)
{

	workerctx_t *wp = (workerctx_t *)arg;

#ifdef HAVE___THREAD
	workerctx_cur = wp;
#else /* HAVE___THREAD */
	/* set pointer to worker context as TSD */
	pthread_setspecific(workerctx_key, wp);
#endif /* HAVE___THREAD */

#ifdef USE_MCTX_PCL
	assert(0 == co_thread_init());
	wp->mctx = co_current();
#endif
  wp->terminate = 0;
	LpelThreadAssign(wp->wid);
  WrapperLoop(wp);
  WORKER_DBG("wrapper: All done wait for SNETGLOBWAIT\n");
  while(SNETGLOBWAIT != SNETGLOBWAITVAL);

#ifdef USE_MCTX_PCL
	co_thread_cleanup();
#endif
	return NULL;
}

/** return the total number of workers, including master */
int LpelWorkerCount(void)
{
  return num_workers;
}


/*******************************************************************************
 * WORKER FUNCTION
 ******************************************************************************/

void LpelWorkerBroadcast(workermsg_t *msg)
{
	int i;
	mailbox_t *wmb;

	for(i=0; i<num_workers; i++) {
		wmb = workermbs[i];
		/* send */
		LpelMailboxSend(wmb, msg);    
	}
}



static void WorkerLoop(workerctx_t *wc)
{
	WORKER_DBG("start worker %d\n", wc->wid);

  lpel_task_t *t = NULL;
  WORKER_DBG("worker: Request work for first time\n");
  requestTask(wc);		// ask for the first time

  workermsg_t msg;
  do {
  	  LpelMailboxRecv(wc->mailbox, &msg);
      WORKER_DBG("\n\n\nworker: MSG received, handle it! %f\n",SCCGetTime());
      
  	  switch(msg.type) {
  	  case WORKER_MSG_ASSIGN:
  	  	t = msg.body.task;
  	  	WORKER_DBG("worker %d: got task %d, isWrapper: %d\n", wc->wid, t->uid,t->wrapper);
        assert(t->state == TASK_READY);
  	  	t->worker_context = wc;
  	  	wc->current_task = t;
#ifdef USE_LOGGING
  	  	if (wc->mon && MON_CB(worker_waitstop)) {
  	  		MON_CB(worker_waitstop)(wc->mon);
  	  	}
  	  	if (t->mon && MON_CB(task_assign)) {
  	  		MON_CB(task_assign)(t->mon, wc->mon);
  	  	}
#endif
  	  	mctx_switch(&wc->mctx, &t->mctx);
  	  	//task return here
  	  	assert(t->state != TASK_RUNNING);

  	  	wc->current_task = NULL;
 	  	  t->worker_context = NULL;
        WORKER_DBG("worker %d: returned task %d\n", wc->wid, t->uid);
	  	  returnTask(t);
        break;
        
  	  case WORKER_MSG_TERMINATE:
        WORKER_DBG("worker: Termination message\n");
  	  	wc->terminate = 1;
  	  	break;
        
  	  default:
  	  	assert(0);
  	  	break;
  	  }
  	  // reach here --> message request for task has been sent
      WORKER_DBG("!(wc->terminate) %d\n\n",(!(wc->terminate)));
  } while (!(wc->terminate) );
}


void *WorkerThread(void *arg)
{
  workerctx_t *wc = (workerctx_t *)arg;

#ifdef HAVE___THREAD
  workerctx_cur = wc;
#else /* HAVE___THREAD */
  /* set pointer to worker context as TSD */
  pthread_setspecific(workerctx_key, wc);
#endif /* HAVE___THREAD */


//FIXME
#ifdef USE_MCTX_PCL
  assert(0 == co_thread_init());
  wc->mctx = co_current();
#endif

  wc->terminate = 0;
  wc->current_task = NULL;
	
	WorkerLoop(wc);
  
#ifdef USE_LOGGING
  /* cleanup monitoring */
  if (wc->mon && MON_CB(worker_destroy)) {
    MON_CB(worker_destroy)(wc->mon);
  }
#endif

#ifdef USE_MCTX_PCL
  co_thread_cleanup();
#endif
  return NULL;
}




workerctx_t *LpelWorkerSelf(void){
#ifdef HAVE___THREAD
  return workerctx_cur;
#else /* HAVE___THREAD */
  return (workerctx_t *) pthread_getspecific(workerctx_key);
#endif /* HAVE___THREAD */
}


lpel_task_t *LpelWorkerCurrentTask(void)
{
	 workerctx_t *w = LpelWorkerSelf();
	  /* It is quite a common bug to call LpelWorkerCurrentTask() from a non-task context.
	   * Provide an assertion error instead of just segfaulting on a null dereference. */
	  assert(w && "Currently not in an LPEL worker context!");
	  return w->current_task;
}


/******************************************
 * TASK RELATED FUNCTIONS
 ******************************************/

void LpelWorkerTaskExit(lpel_task_t *t) {
	workerctx_t *wc = t->worker_context;
	WORKER_DBG("worker %d: task %d exit\n", wc->wid, t->uid);
	if (wc->wid >= 0) {
		requestTask(wc);	// FIXME: should have requested before
		wc->current_task = NULL;
	} else {
		wc->terminate = 1;		// wrapper: terminate
  }
  ALL_DBG("worker_op.c: worker %d, task %d exit, wc->ter %d\n", wc->wid, t->uid, wc->terminate);
  mctx_switch(&t->mctx, &wc->mctx);		// switch back to the worker
}


void LpelWorkerTaskBlock(lpel_task_t *t){
	workerctx_t *wc = t->worker_context;

	if (wc->wid >= 0){ // wrapper does not need to request task
		WORKER_DBG("worker %d: block task %d\n", wc->wid, t->uid);
		//sendUpdatePrior(t);		//update prior for neighbour
		requestTask(wc);
	}
	wc->current_task = NULL;
	ALL_DBG("worker_op.c: TaskBlock switch ");
	mctx_switch(&t->mctx, &wc->mctx);		// switch back to the worker/wrapper
}

void LpelWorkerTaskYield(lpel_task_t *t){
	workerctx_t *wc = t->worker_context;

	if (wc->wid >= 0){
		//sendUpdatePrior(t);		//update prior for neighbor
		requestTask(wc);
		WORKER_DBG("worker %d: return task %d\n", wc->wid, t->uid);
		wc->current_task = NULL;
	}
	mctx_switch(&t->mctx, &wc->mctx);		// switch back to the worker/wrapper
}

void LpelWorkerTaskWakeup(lpel_task_t *t) {
	workerctx_t *wc = t->worker_context;
	WORKER_DBG("worker %d: send wake up task %d\n", LpelWorkerSelf()->wid, t->uid);
	
  if (wc == NULL || wc->wid >= 0){
    sendWakeup(mastermb, t);
    ALL_DBG("worker %d: send wake up to task %d via masterMB\n", LpelWorkerSelf()->wid, t->uid);
  } else {
    sendWakeup(wc->mailbox, t);
    ALL_DBG("worker %d: send wake up to task %d at mailbox %p\n", LpelWorkerSelf()->wid, t->uid, wc->mailbox);
  }
}



/******************************************
 * STREAM RELATED FUNCTIONS
 ******************************************/
void LpelWorkerPutStream(workerctx_t *wc, lpel_stream_t *s) {
	if (wc->free_stream == NULL) {
		wc->free_stream = s;
		s->next = NULL;
	} else {
		s->next = wc->free_stream;
		wc->free_stream = s;
	}
}

lpel_stream_t *LpelWorkerGetStream() {
	lpel_stream_t *tmp;
	workerctx_t *wc = LpelWorkerSelf();
	if (wc == NULL) {
		return NULL;
	}

	tmp = wc->free_stream;
	if (tmp) {
		wc->free_stream = tmp->next;
		tmp->next = NULL;
		assert(tmp->cons_sd == NULL && tmp->prod_sd == NULL);
	}
	return tmp;
}

void LpelWorkerPutSd(workerctx_t *wc, lpel_stream_desc_t *sd) {
	if (wc->free_sd == NULL) {
		wc->free_sd = sd;
		sd->next = NULL;
	} else {
		sd->next = wc->free_sd;
		wc->free_sd = sd;
	}
}

lpel_stream_desc_t *LpelWorkerGetSd(workerctx_t *wc) {
	lpel_stream_desc_t *tmp = wc->free_sd;
	if (tmp != NULL) {
		if (tmp->task == NULL && tmp->stream == NULL) {
			wc->free_sd = tmp->next;
			tmp->next = NULL;
			return tmp;
		} else {
			lpel_stream_desc_t *prev = tmp;
			tmp = tmp->next;
			while (tmp != NULL) {
				if (tmp->task == NULL && tmp->stream == NULL) {
					prev->next = tmp->next;
					tmp->next = NULL;
					return tmp;
				}
				prev = tmp;
				tmp = tmp->next;
			}
		}
	}
	return NULL;
}

void LpelWorkerDestroyStream(workerctx_t *wc) {
	lpel_stream_t *head = wc->free_stream;
	lpel_stream_t *tmp;
	while (head != NULL) {
		tmp = head->next;
		LpelStreamDestroy(head);
		head = tmp;
	}
}

void LpelWorkerDestroySd(workerctx_t *wc) {
	lpel_stream_desc_t *head = wc->free_sd;
	lpel_stream_desc_t *tmp;
	while (head != NULL) {
		tmp = head->next;
		SCCFreePtr(head);
		head = tmp;
	}
}
