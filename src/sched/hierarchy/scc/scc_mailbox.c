#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include "mailbox.h"
#include "hrc_lpel.h"
#include "scc.h"

extern uintptr_t *allMbox;
pthread_mutexattr_t attr;

/* Utility functions*/
typedef int bool;
#define false 0
#define true  1

/* mailbox structures */

typedef struct mailbox_node_t {
  struct mailbox_node_t *next;
  workermsg_t msg;
} mailbox_node_t;

struct mailbox_t {
  pthread_mutex_t     lock_inbox;
  volatile mailbox_node_t      *list_free;
  volatile mailbox_node_t      *list_inbox;
  mailbox_node_t      *tail;
  int                 mbox_ID;
};

//#define _USE_MBX_DBG__

#ifdef _USE_MBX_DBG__
#define MAILBOX_DBG_LOCK printf
#define MAILBOX_DBG printf
//#define MAILBOX_DBG_LIST
#else
#define MAILBOX_DBG_LOCK //
#define MAILBOX_DBG	//
#endif

void printListInbox(char* c, mailbox_t *mbox);

/******************************************************************************/
/* Free node pool management functions                                        */
/******************************************************************************/

static mailbox_node_t *GetFree( mailbox_t *mbox)
{
  mailbox_node_t *node;

  if (mbox->list_free != NULL) {
    /* pop free node off */
    node = mbox->list_free;
    mbox->list_free = node->next; /* can be NULL */
  } else {
    /* allocate new node */
    node = (mailbox_node_t *) SCCMallocPtr( sizeof( mailbox_node_t));
  }
  
  node->next = NULL;
  return node;
}

static void PutFree( mailbox_t *mbox, mailbox_node_t *node)
{
  if ( mbox->list_free == NULL) {
    node->next = NULL;
  } else {
    node->next = mbox->list_free;
  }
  mbox->list_free = node;
}

/******************************************************************************/
/* Public functions                                                           */
/******************************************************************************/

mailbox_t *LpelMailboxCreate(void)
{
  mailbox_t *mbox = allMbox[SCCGetNodeRank()];

  pthread_mutexattr_init( &attr);
  pthread_mutexattr_setpshared( &attr, PTHREAD_PROCESS_SHARED);
  
  pthread_mutex_init( &mbox->lock_inbox, &attr);
  mbox->list_free  = NULL;
  mbox->list_inbox = NULL;
  mbox->tail = NULL;
  mbox->mbox_ID = SCCGetNodeID();

  return mbox;
}

void LpelMailboxDestroy( mailbox_t *mbox)
{
  if (mbox == NULL || mbox->list_inbox != NULL ) return;
  mailbox_node_t *node;

  assert( mbox->list_inbox == NULL);
  /* free all free nodes */
  
  while (mbox->list_free != NULL) {
    /* pop free node off */
    node = mbox->list_free;  
    mbox->list_free = node->next; /* can be NULL */
    /* free the memory for the node */
    SCCFreePtr( node);   
  }

  /* destroy sync primitives */
  pthread_mutex_destroy( &mbox->lock_inbox);

  //free(mbox); // as mbox is not allocated
  
  /* destroy an attribute */
  pthread_mutexattr_destroy(&attr);
}

void LpelMailboxSend( mailbox_t *mbox, workermsg_t *msg)
{
  int value=-1;
  while(value != 0){
    atomic_incR(&atomic_inc_regs[mbox->mbox_ID],&value);
  }

  MAILBOX_DBG_LOCK("Mailbox send: locked %d at %f\n",mbox->mbox_ID,SCCGetTime());
    
  /* get a free node from recepient */
  mailbox_node_t *node = GetFree( mbox);

  /* copy the message */
  node->msg = *msg;
  printListInbox("Send bfor",mbox);
  
  /* put node into inbox */
  node->next = NULL;
  if ( mbox->tail == NULL) {
    mbox->list_inbox = mbox->tail = node;
  } else {
    mbox->tail->next = node;
    mbox->tail = node;
  }
  
  printListInbox("Send aftr",mbox);
  MAILBOX_DBG_LOCK("Mailbox send: unlocked %d at %f\n\n",mbox->mbox_ID,SCCGetTime());
  
  atomic_writeR(&atomic_inc_regs[mbox->mbox_ID],0);
  //printf("-----------------------------------> msg sent to %d\n",mbox->mbox_ID);
}


void LpelMailboxRecv( mailbox_t *mbox, workermsg_t *msg)
{
	long counter = 0;
  mailbox_node_t *node;
  bool message=false,go_on=false;

  while(go_on==false){
  	counter++;
    if(mbox->list_inbox != NULL){
      int value=-1;
      while(value != 0){
        atomic_incR(&atomic_inc_regs[mbox->mbox_ID],&value);
      }     	
      MAILBOX_DBG_LOCK("Mailbox recv: locked %d at %f\n",mbox->mbox_ID,SCCGetTime());
      go_on=true;
    } /*else {
    	counter++;
	    //printf(". ");
    	if(mbox->list_inbox == NULL && counter == 1000) { 
    		//printf(". %d\t",mbox->list_inbox); 
    		counter = 0; 
    	}
    }*/
  }

  assert( mbox->list_inbox != NULL);
  printListInbox("Recv bfor",mbox);

  node = mbox->list_inbox;
  if (node->next == NULL){
    mbox->list_inbox = mbox->tail = NULL;
  } else {
    mbox->list_inbox = node->next;
  }

  /* copy the message */
  *msg = node->msg;
  int wid = msg->body.from_worker;
  /* put node into free pool */
  PutFree( mbox, node);
  printListInbox("Recv aftr",mbox);
  MAILBOX_DBG_LOCK("Mailbox recv: unlocked %d at %f\n\n",mbox->mbox_ID,SCCGetTime());
  
  atomic_writeR(&atomic_inc_regs[mbox->mbox_ID],0);
  //printf("-----------------------------------> msg received from %d\n",wid);
}

/**
 * @return 1 if there is an incoming msg, 0 otherwise
 * @note: does not need to be locked as a 'missed' msg
 *        will be eventually fetched in the next worker loop
 */
int LpelMailboxHasIncoming( mailbox_t *mbox)
{
  return ( mbox->list_inbox != NULL);
}

void printListInbox(char* c, mailbox_t *mbox){
#ifdef MAILBOX_DBG_LIST
  mailbox_node_t *node;
  printf("%s at %f: mbox %p, id %d , mbox->list_inbox = ",c,SCCGetTime(),mbox,mbox->mbox_ID);
  if(mbox->list_inbox != NULL){
    node =  mbox->list_inbox;
    do{
    printf("%p-> ",node);
    node = node->next;
    } while(node != NULL);
  }
  printf("NULL\n");
#endif
}
