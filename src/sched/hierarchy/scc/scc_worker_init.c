/*
 * scc_worker_init.c
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

#include <pthread.h>
#include "arch/mctx.h"

#include "arch/atomic.h"

#include "../hrc_worker.h"
#include "../hrc_task.h"
#include "lpel_hwloc.h"
#include "lpelcfg.h"
#include "../hrc_stream.h"
#include "mailbox.h"
#include "lpel/monitor.h"
#include "lpel_main.h"
#include "scc.h"


//#define _USE_WORKER_DBG__

#ifdef _USE_WORKER_DBG__
#define WORKER_DBG printf
#else
#define WORKER_DBG	//
#endif

static void cleanupMasterMb();

static int num_workers = -1;
static masterctx_t *master;
static workerctx_t *worker;
static int node_ID;
static void *local;
/**
 * Initialise worker globally
 *
 *
 * @param size    size of the worker set, i.e., the total number of workers including master
 */
void LpelWorkersInit(int size) {

	int i;
	assert(0 <= size);
	num_workers = size - 1;
  
	/* local variables used in worker operations */
	initLocalVar(num_workers);
  
  /*ini mailbox*/
  node_ID=SCCGetNodeID();
  //LpelMailboxInit(node_ID,num_workers);
  LpelMailboxInit(SCCGetNodeRank(),num_workers);
  mailbox_t *mbox =  LpelMailboxCreate();
  
  if (node_ID == master_ID) {
    /** create master */
    master = (masterctx_t *) malloc(sizeof(masterctx_t));
    master->mailbox = mbox;
    //printf("Master mailbox: %p\n",master->mailbox);
    //master->mailbox = allmbox[node_ID];
    master->ready_tasks = LpelTaskqueueInit ();
    master->num_workers = num_workers;
    /*master do not hold context for workers*/
    master->workers = NULL;
    /* allocate waiting table */
    master->waitworkers = (int *) malloc(num_workers * sizeof(int));
    for (i=0; i<num_workers; i++) {
      master->waitworkers[i] = 0;
    }
  } else{
    /*create single worker per core*/
    worker=(workerctx_t *) malloc(sizeof(workerctx_t));
    //worker->wid=node_ID-1;
    worker->wid=SCCGetNodeRank()-1;
#ifdef USE_LOGGING
    if (MON_CB(worker_create)) {
      worker->mon = MON_CB(worker_create)(worker->wid);
    } else {
      worker->mon = NULL;
    }
#else
    worker->mon = NULL;
#endif
    /* mailbox */
    worker->mailbox = mbox;
    //worker->mailbox = allmbox[node_ID];
    //printf("Worker mailbox: %p\n",worker->mailbox);
    worker->free_sd = NULL;
    worker->free_stream = NULL;
  }
}


void setupMailbox(mailbox_t **mastermb, mailbox_t **workermbs) {
   int i;
   *mastermb = allMbox[0];
   WORKER_DBG("\nmastermb %p\n",*mastermb);
   for(i=0;i<num_workers;i++){
    workermbs[i] = allMbox[i+1];
    WORKER_DBG("workermbs[%d] %p\n",i,workermbs[i]);
   }
}

/*
 * clean up for both worker and master
 */
void LpelWorkersCleanup(void) {
	int i;

        if (node_ID == master_ID) {
          /* wait for the master to finish */
          (void) pthread_join(master->thread, NULL);
          /* clean up master's mailbox */
          cleanupMasterMb();
          LpelMailboxDestroy(master->mailbox);
          LpelTaskqueueDestroy(master->ready_tasks);

          /* free workers tables */
          free(master->waitworkers);
          free(master);
          WORKER_DBG("CLEAN; master finished");
        } else {
          /* wait for the worker to finish */
          (void) pthread_join(worker->thread, NULL);
          LpelMailboxDestroy(worker->mailbox);
          LpelWorkerDestroyStream(worker);
          LpelWorkerDestroySd(worker);
          free(worker);
          WORKER_DBG("CLEAN; worker finished");
        }
        /* clean up local vars used in worker operations */
        cleanupLocalVar();
}


/*
 * Spawn master and workers
 */
void LpelWorkersSpawn(void) {
	if (node_ID == master_ID) {
          /* master spawn joinable thread*/
          (void) pthread_create(&master->thread, NULL, MasterThread, master);
        } else {
          /* workers */
          (void) pthread_create(&worker->thread, NULL, WorkerThread, worker);
	}
}


/*
 * Terminate master and workers
 */
void LpelWorkersTerminate(void) {
	workermsg_t msg;
	msg.type = WORKER_MSG_TERMINATE;
	LpelMailboxSend(master->mailbox, &msg);
}

/************************ Private functions ***********************************/
/*
 * clean up master's mailbox before terminating master
 * last messages including: task request from worker, and return zombie task
 */
static void cleanupMasterMb() {
	workermsg_t msg;
	lpel_task_t *t;
	while (LpelMailboxHasIncoming(master->mailbox)) {
		LpelMailboxRecv(master->mailbox, &msg);
		switch(msg.type) {
		case WORKER_MSG_REQUEST:
			break;
		case WORKER_MSG_RETURN:
			t = msg.body.task;
			WORKER_DBG("master: get returned task %d\n", t->uid);
	    assert(t->state == TASK_ZOMBIE);
			LpelTaskDestroy(t);
			break;
		default:
			assert(0);
			break;
		}
	}
}
