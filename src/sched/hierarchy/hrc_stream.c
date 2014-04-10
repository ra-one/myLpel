#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <hrc_lpel.h>

#include "arch/atomic.h"
#include "lpelcfg.h"
#include "hrc_task.h"
#include "hrc_worker.h"
#include "hrc_stream.h"
#include "lpel/monitor.h"


//#define _USE_STREAM_DBG__

#ifdef _USE_STREAM_DBG__
#define STREAM_DBG printf
#else
#define STREAM_DBG	//
#endif

#define STRM_LOCK_DBG(x) fprintf(stderr,x)

static atomic_int stream_seq = ATOMIC_VAR_INIT(0);

// print stream info for debug
void streamPrint (lpel_stream_t *s, char *p){}
void streamPrint2(lpel_stream_t *s,lpel_task_t *t, char *p){}
void streamPrint21(lpel_stream_t *s,lpel_task_t *t, char *p){
  if(s->uid < 100){
    if(t != NULL) fprintf(stderr,"task: %d %p, stream id: %d %p %s\n",t->uid,t,s->uid,s,p);
    else fprintf(stderr,"stream id: %d %p %s\n",s->uid,s,p);
  }
}

void streamPrint1(lpel_stream_t *s, char *p){
  printf("stream id: %d %p, prodlock addr: %p, from %s\n",s->uid,s,&s->prod_lock,p);
}

void taskPrint(lpel_stream_desc_t *sd, lpel_task_t *slf, char *p){
  if(sd == NULL){
    printf("from: %s sd=NULL\n task %p %d, t->wrapper %d,state :%c:\n\n\n",p,slf,slf->uid,slf->wrapper,slf->state);
  } else {
    printf("from: %s\ntask %p %d, t->wrapper %d,state :%c:\ntask %p %d, t->wrapper %d,state :%c:\n\n\
    \n",p,sd->task,sd->task->uid,sd->task->wrapper,sd->task->state,slf,slf->uid,slf->wrapper,slf->state);
  }
}

char* getStreamType(lpel_stream_type type){
  char *retval;
  if(type == LPEL_STREAM_ENTRY)       retval = "LPEL_STREAM_ENTRY";
  else if(type == LPEL_STREAM_EXIT)   retval = "LPEL_STREAM_EXIT";
  else if(type == LPEL_STREAM_MIDDLE) retval = "LPEL_STREAM_MIDDLE";
  else                                retval = "STREAM TYPE UNKNOWN";
  
  return retval;       
}

/**
 * Create a stream
 *
 * Allocate and initialize memory for a stream.
 *
 * @return pointer to the created stream
 */
lpel_stream_t *LpelStreamCreate(int size)
{
  assert( size >= 0);
  if (0==size) size = STREAM_BUFFER_SIZE;

  lpel_stream_t *s;

  s = (lpel_stream_t *) SCCMallocPtr( sizeof(lpel_stream_t) );
  LpelBufferInit( &s->buffer, size);
  
  assert(LpelBufferIsEmpty(&s->buffer));

  s->uid = (SCCGetNodeRank()*100)+atomic_fetch_add( &stream_seq, 1);
  pthread_mutexattr_t attr;
  pthread_mutexattr_init( &attr);
  pthread_mutexattr_setpshared( &attr, PTHREAD_PROCESS_SHARED);
  pthread_mutex_init(&s->prod_lock,&attr);
  pthread_mutexattr_destroy(&attr);
  
  streamPrint(s,"init");
  atomic_init( &s->n_sem, 0);
  atomic_init( &s->e_sem, size);
  s->is_poll = 0;
  s->prod_sd = NULL;
  s->cons_sd = NULL;
  s->usr_data = NULL;
  s->type = LPEL_STREAM_MIDDLE;
  s->next = NULL;
  s->read_cnt = 0;
  s->write_cnt = 0;
  return s;
}


/**
 * Destroy a stream
 *
 * Free the memory allocated for a stream.
 *
 * @param s   stream to be freed
 * @pre       stream must not be opened by any task!
 */
void LpelStreamDestroy( lpel_stream_t *s)
{
  streamPrint(s,"destroy");
  pthread_mutex_destroy(&s->prod_lock);
  atomic_destroy( &s->n_sem);
  atomic_destroy( &s->e_sem);
  LpelBufferCleanup( &s->buffer);
  SCCFreePtr( s);
}


/**
 * Store arbitrary user data in stream
 * CAUTION use at own risk
 */
void LpelStreamSetUsrData(lpel_stream_t *s, void *usr_data)
{
  s->usr_data = usr_data;
}

/**
 * Load user data from stream
 * CAUTION use at own risk
 */
void *LpelStreamGetUsrData(lpel_stream_t *s)
{
  return s->usr_data;
}


/**
  * Open a stream for reading/writing
 *
 * @param s     pointer to stream
 * @param mode  either 'r' for reading or 'w' for writing
 * @return      a stream descriptor
 * @pre         only one task may open it for reading resp. writing
 *              at any given point in time
 */
lpel_stream_desc_t *LpelStreamOpen( lpel_stream_t *s, char mode)
{
  lpel_stream_desc_t *sd;
  lpel_task_t *ct = LpelTaskSelf();

  assert( mode == 'r' || mode == 'w' );

  sd = (lpel_stream_desc_t *) SCCMallocPtr( sizeof( lpel_stream_desc_t));
  sd->task = ct;
  sd->stream = s;
  sd->mode = mode;
  sd->next  = NULL;

  sd->mon = NULL;

  switch(mode) {
    case 'r': s->cons_sd = sd; break;
    case 'w': s->prod_sd = sd; break;
  }

  /* set entry/exit stream */
  if (LpelTaskIsWrapper(ct))
  	s->type = (mode == 'r' ? LPEL_STREAM_EXIT : LPEL_STREAM_ENTRY);
  
  //if(s->uid < 100)fprintf(stderr,"task %d opened stream %d mode %c as %d %s\n", ct->uid,s->uid,mode,s->type,getStreamType(s->type));
  STREAM_DBG("task %d open stream %d, mode %c\n", ct->uid, s->uid, mode);
  LpelTaskAddStream(ct, sd, mode);
  streamPrint2(s,NULL,"open");
  return sd;
}

/**
 * Close a stream previously opened for reading/writing
 *
 * @param sd          stream descriptor
 * @param destroy_s   if != 0, destroy the stream as well
 */
void LpelStreamClose( lpel_stream_desc_t *sd, int destroy_s)
{
  streamPrint2(sd->stream,sd->task,"close");

  STREAM_DBG("task %d closes stream %d, mode %c\n", sd->task->uid, sd->stream->uid, sd->mode);
  workerctx_t *wc = sd->task->worker_context;
  if (destroy_s) {
  	STREAM_DBG("task %d destroy stream %d, mode %c\n", sd->task->uid, sd->stream->uid, sd->mode);
  	assert(sd->mode == 'r');
  	lpel_stream_t *s = sd->stream;
  	assert(LpelBufferIsEmpty(&s->buffer));

  	/* free the stream structure */
  	s->prod_sd->stream = NULL;			// unset the stream pointer of producer
  	s->prod_sd = NULL;							// unset producer
  	s->cons_sd = NULL;							// unset consumer
  	LpelWorkerPutStream(wc, s);				// put back to worker's free list
  	sd->stream = NULL;
  }
  LpelTaskRemoveStream(sd->task, sd, sd->mode);
  sd->task = NULL;								// unset only the pointer to task
  LpelWorkerPutSd(wc, sd);				// put back to worker's free list
}


/**
 * Replace a stream opened for reading by another stream
 * Destroys old stream.
 *
 * @param sd    stream descriptor for which the stream must be replaced
 * @param snew  the new stream
 * @pre         snew must not be opened by same or other task
 */
void LpelStreamReplace( lpel_stream_desc_t *sd, lpel_stream_t *snew)
{
  assert( sd->mode == 'r');
  STREAM_DBG("task %d replace stream %d by stream %d, mode %c\n", sd->task->uid, sd->stream->uid, snew->uid, sd->mode);

  workerctx_t *wc = sd->task->worker_context;
  lpel_stream_t *s = sd->stream;
  snew->type = s->type;

  /* free the old stream */
  s->prod_sd->stream = NULL;
  s->prod_sd = NULL;
  s->cons_sd = NULL;
  assert(LpelBufferIsEmpty(&s->buffer));
  LpelWorkerPutStream(wc, s);

  /* assign new stream */
  lpel_stream_desc_t *old_cons = snew->cons_sd;
  old_cons->stream = NULL;			// unset the stream pointer of the old consumer
  snew->cons_sd = sd;
  sd->stream = snew;
  
  fprintf(stderr,"\n\n\n\n\t*********************************************************************\n");
}


/**
 * Get the stream opened by a stream descriptor
 *
 * @param sd  the stream descriptor
 * @return    the stream opened by the stream descriptor
 */
lpel_stream_t *LpelStreamGet(lpel_stream_desc_t *sd)
{
  return sd->stream;
}



/**
 * Non-blocking, non-consuming read from a stream
 *
 * @param sd  stream descriptor
 * @return    the top item of the stream, or NULL if stream is empty
 */
void *LpelStreamPeek( lpel_stream_desc_t *sd)
{
  assert( sd->mode == 'r');
  return LpelBufferTop( &sd->stream->buffer);
}


/**
 * Blocking, consuming read from a stream
 *
 * If the stream is empty, the task is suspended until
 * a producer writes an item to the stream.
 *
 * @param sd  stream descriptor
 * @return    the next item of the stream
 * @pre       current task is single reader
 */
void *LpelStreamRead( lpel_stream_desc_t *sd)
{
  void *item;
  lpel_task_t *self = sd->task;
  if (sd->mode != 'r' ) sd->mode = 'r';
  assert( sd->mode == 'r');
  streamPrint2(sd->stream,self,"read");
  
  /* quasi P(n_sem) */
  if ( atomic_fetch_sub( &sd->stream->n_sem, 1) == 0) {
    /* wait on stream: */
    taskPrint(sd, self, "Read");printf("WILL BLOCK FROM READ\n");
    LpelTaskBlockStream( self);
  }

  /* read the top element */
  do{
    item = LpelBufferTop( &sd->stream->buffer);
  } while (item == NULL);
  assert( item != NULL);
  /* pop off the top element */
  LpelBufferPop( &sd->stream->buffer);
  sd->stream->read_cnt++;
  
  /* only entry stream is bounded */
  if (sd->stream->type == LPEL_STREAM_ENTRY) {
  	/* quasi V(e_sem) */
  	if ( atomic_fetch_add( &sd->stream->e_sem, 1) < 0) {
  		/* e_sem was -1 */
  		lpel_task_t *prod = sd->stream->prod_sd->task;
  		/* wakeup producer: make ready */
  		LpelTaskUnblock(prod);
  	}
  }
  return item;
}



/**
 * Blocking write to a stream
 *
 * If the stream is full, the task is suspended until the consumer
 * reads items from the stream, freeing space for more items.
 *
 * @param sd    stream descriptor
 * @param item  data item (a pointer) to write
 * @pre         current task is single writer
 * @pre         item != NULL
 */
void LpelSdPrint( lpel_stream_desc_t *sd)
{
  printf("LpelSdPrint: sd %p, task %d, t->wrapper %d,state :%c: ctx %p, wid %d, wctx %p\n\n",sd,sd->task->uid,sd->task->wrapper,sd->task->state,sd->task->worker_context,sd->task->worker_context->wid,sd->task->worker_context->mctx);
}
void LpelStreamWrite( lpel_stream_desc_t *sd, void *item)
{
  lpel_task_t *self = sd->task;
  printf("LpelStreamWrite se: sd %p, task %d, t->wrapper %d,state :%c: ctx %p, wid %d, wctx %p\n\n",sd,self->uid,self->wrapper,self->state,self->worker_context,self->worker_context->wid,self->worker_context->mctx);
  printf("LpelStreamWrite sd: sd %p, task %d, t->wrapper %d,state :%c: ctx %p, wid %d, wctx %p\n\n",sd,sd->task->uid,sd->task->wrapper,sd->task->state,sd->task->worker_context,sd->task->worker_context->wid,sd->task->worker_context->mctx);
  streamPrint2(sd->stream,self,"write");
  int poll_wakeup = 0;

  /* check if opened for writing */
  if (item == NULL ) while(item == NULL );
  assert( item != NULL );

  /* only entry stream is bounded */
  if (sd->stream->type == LPEL_STREAM_ENTRY) {
  	/* quasi P(e_sem) */
  	if ( atomic_fetch_sub( &sd->stream->e_sem, 1)== 0) {
  		/* wait on stream: */
      //taskPrint(sd, self, "Write");
  	  printf("WILL BLOCK FROM WRITE 1\n");
      LpelTaskBlockStream( self);
  	}
  }

  /* writing to the buffer and checking if consumer polls must be atomic */
  streamPrint(sd->stream,"going to lock in write");
write:;
  int count = 0;
  do{
    if(count++ > 1000){ 
      pthread_mutex_unlock(&sd->stream->prod_lock);
      usleep(sd->stream->uid); 
      goto write;
    }    
  } while(pthread_mutex_trylock(&sd->stream->prod_lock) != 0);
  {
    /* there must be space now in buffer */
    assert( LpelBufferIsSpace( &sd->stream->buffer) );
    /* put item into buffer */
    LpelBufferPut( &sd->stream->buffer, item);
    sd->stream->write_cnt++;
    if ( sd->stream->is_poll) {
      /* get consumer's poll token */
      poll_wakeup = atomic_exchange( &sd->stream->cons_sd->task->poll_token, 0);
      sd->stream->is_poll = 0;
    }
  }
  streamPrint(sd->stream,"going to unlock in write");
  pthread_mutex_unlock(&sd->stream->prod_lock);

  /* quasi V(n_sem) */
  if ( atomic_fetch_add( &sd->stream->n_sem, 1) < 0) {
    /* n_sem was -1 */
    lpel_task_t *cons = sd->stream->cons_sd->task;
    /* wakeup consumer: make ready */
    LpelTaskUnblock(cons);
    printf("Task %d unblocked 1\n",cons->uid);
  } else {
    /* we are the sole producer task waking the polling consumer up */
    if (poll_wakeup) {
      lpel_task_t *cons = sd->stream->cons_sd->task;
      cons->wakeup_sd = sd->stream->cons_sd;
      LpelTaskUnblock(cons);
      printf("Task %d unblocked 2\n",cons->uid);
    }
  }
 	//LpelTaskCheckYield(self);
}



/**
 * Non-blocking write to a stream
 *
 * @param sd    stream descriptor
 * @param item  data item (a pointer) to write
 * @pre         current task is single writer
 * @pre         item != NULL
 * @return 0 if the item could be written, -1 if the stream was full
 */
int LpelStreamTryWrite( lpel_stream_desc_t *sd, void *item)
{
  if (!LpelBufferIsSpace(&sd->stream->buffer)) {
    return -1;
  }
  LpelStreamWrite( sd, item );
  return 0;
}

/**
 * Poll a set of streams
 *
 * This is a blocking function called by a consumer which wants to wait
 * for arrival of data on any of a specified set of streams.
 * The consumer task is suspended while there is no new data on all streams.
 *
 * @param set     a stream descriptor set the task wants to poll
 * @pre           set must not be empty (*set != NULL)
 *
 * @post          The first element when iterating through the set after
 *                LpelStreamPoll() will be the one after the one which
 *                caused the task to wakeup,
 *                i.e., the first stream where data arrived.
 */
lpel_stream_desc_t *LpelStreamPoll( lpel_streamset_t *set)
{
  printf("\n******************************* starting poll\n");
  lpel_task_t *self;
  lpel_stream_iter_t *iter;
  int do_ctx_switch = 1;
  int cnt = 0;

  assert( *set != NULL);

  /* get 'self', i.e. the task calling LpelStreamPoll() */
  self = (*set)->task;

  iter = LpelStreamIterCreate( set);

  /* fast path*/
  while( LpelStreamIterHasNext( iter)) {
    lpel_stream_desc_t *sd = LpelStreamIterNext( iter);
    lpel_stream_t *s = sd->stream;
    if ( LpelBufferTop( &s->buffer) != NULL) {
      LpelStreamIterDestroy(iter);
      *set = sd;
      printf("\n******************************* finished poll 1\n");
      return sd;
    }
  }


  /* place a poll token */
  atomic_store( &self->poll_token, 1);

  /* for each stream in the set */
  LpelStreamIterReset(iter, set);
  while( LpelStreamIterHasNext( iter)) {
    lpel_stream_desc_t *sd = LpelStreamIterNext( iter);
    lpel_stream_t *s = sd->stream;
    /* lock stream (prod-side) */
    streamPrint(s,"going to lock in poll 1");
poll1:;
    int count = 0;
    do{
      if(count++ > 1000) { 
        pthread_mutex_unlock(&s->prod_lock);
        //usleep((s->uid)+1000); 
        usleep((s->uid)+(SCCGetNodeRank()*10)); 
        goto poll1;
      }
    }while(pthread_mutex_trylock(&s->prod_lock) != 0);
    { /* CS BEGIN */
      /* check if there is something in the buffer */
      if ( LpelBufferTop( &s->buffer) != NULL) {
        /* yes, we can stop iterating through streams.
         * determine, if we have been woken up by another producer:
         */
        int tok = atomic_exchange( &self->poll_token, 0);
        if (tok) {
          /* we have not been woken yet, no need for ctx switch */
          do_ctx_switch = 0;
          self->wakeup_sd = sd;
        }
        /* unlock stream */
        streamPrint(s,"going to unloc poll 1");
        pthread_mutex_unlock( &s->prod_lock);
        /* exit loop */
        break;

      } else {
        /* nothing in the buffer, register stream as activator */
        s->is_poll = 1;
        cnt++;
        //sd->event_flags |= STDESC_WAITON;
        /* TODO marking all streams does potentially flood the log-files
           - is it desired to have anyway?
        MarkDirty( sd);
        */
      }
    } /* CS END */
    /* unlock stream */
    streamPrint(s,"going to unloc poll 2");
    pthread_mutex_unlock( &s->prod_lock);
  } /* end for each stream */

  /* context switch */
  if (do_ctx_switch) {
    /* set task as blocked */
    taskPrint(NULL, self, "Poll");
    LpelTaskBlockStream( self);
  }
  assert( atomic_load( &self->poll_token) == 0);

  /* unregister activators
   * - would only be necessary, if the consumer task closes the stream
   *   while the producer is in an is_poll state,
   *   as this could result in a SEGFAULT when the producer
   *   is trying to dereference sd->stream->cons_sd
   * - a consumer closes the stream if it reads
   *   a terminate record or a sync record, and between reading the record
   *   and closing the stream the consumer issues no LpelStreamPoll()
   *   and no entity writes a record on the stream after these records.
   * UPDATE: with static/dynamc collectors in S-Net, this is possible!
   */
  LpelStreamIterReset(iter, set);
  while( LpelStreamIterHasNext( iter)) {
    lpel_stream_t *s = (LpelStreamIterNext(iter))->stream;
    streamPrint(s,"going to loc poll 2");
poll2:;
    int count = 0;
    do{
      if(count++ > 1000) { 
        pthread_mutex_unlock(&s->prod_lock);
        //usleep((s->uid)+500); 
        usleep((s->uid)+(SCCGetNodeRank()*20)); 
        goto poll2;
      }
    }while(pthread_mutex_trylock(&s->prod_lock) != 0);
    s->is_poll = 0;
    streamPrint(s,"going to unloc poll 3");
     pthread_mutex_unlock( &s->prod_lock);
    if (--cnt == 0) break;
  }

  LpelStreamIterDestroy(iter);

  /* 'rotate' set to stream descriptor for non-empty buffer */
  *set = self->wakeup_sd;
  printf("\n******************************* finished poll 2\n");
  return self->wakeup_sd;
}

/*
 * get stream level
 * Assumption: MAX_INT as the maximum value of counter
 * 						 in case of large number of message, use modulo MAX_INT to get the correct number
 */
int LpelStreamFillLevel(lpel_stream_t *s) {
	if (s == NULL)
		return 0;
	return (s->write_cnt - s->read_cnt);
}

lpel_task_t *LpelStreamConsumer(lpel_stream_t *s) {
	if (s == NULL)
		return NULL;
	if (s->cons_sd != NULL)
		return s->cons_sd->task;
	else
		return NULL;
}

lpel_task_t *LpelStreamProducer(lpel_stream_t *s) {
	if (s == NULL)
		return NULL;
	if (s->prod_sd != NULL)
		return s->prod_sd->task;
	else
		return NULL;
}

int LpelStreamGetId ( lpel_stream_desc_t *sd){
	if (!sd)
		return -1;
	if (!sd->stream)
		return -1;
	return sd->stream->uid;
}
