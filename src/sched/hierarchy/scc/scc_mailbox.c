#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include "mailbox.h"
#include "scc.h"


extern uintptr_t  addr;
extern mailbox_t **allmbox;
static int node_ID;
static int size;
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
  pthread_mutex_t     lock_free;
  pthread_mutex_t     lock_inbox;
  pthread_mutex_t     notempty;
  mailbox_node_t      *list_free;
  mailbox_node_t      *list_inbox;
  int                 mbox_ID;
};


/******************************************************************************/
/* Free node pool management functions                                        */
/******************************************************************************/

static mailbox_node_t *GetFree( mailbox_t *mbox)
{
  mailbox_node_t *node;

  DCMflush();
  if (mbox->list_free != NULL) {
    /* pop free node off */
    node = mbox->list_free;
    mbox->list_free = node->next; /* can be NULL */
  } else {
    /* allocate new node */
    node = (mailbox_node_t *)malloc( sizeof( mailbox_node_t));
  }
  DCMflush();

  return node;
}

static void PutFree( mailbox_t *mbox, mailbox_node_t *node)
{
  DCMflush();
  if ( mbox->list_free == NULL) {
    node->next = NULL;
  } else {
    node->next = mbox->list_free;
  }
  mbox->list_free = node;
  DCMflush();
}



/******************************************************************************/
/* Public functions                                                           */
/******************************************************************************/

void LpelMailboxInit(int node_id_num, int num_worker){
  node_ID = node_id_num;
  size = num_worker+1; //+1 to include master	
  int i;
  allmbox = (mailbox_t**)malloc(sizeof(mailbox_t*)*size);
  for (i=0; i < size;i++){
    allmbox[i] = (mailbox_t*) addr+MEMORY_OFFSET(i)+0x1190;
  }	
}

mailbox_t *LpelMailboxCreate(void)
{
  mailbox_t *mbox = (mailbox_t *)malloc(sizeof(mailbox_t));

  pthread_mutexattr_init( &attr);
  pthread_mutexattr_setpshared( &attr, PTHREAD_PROCESS_SHARED);
  
  pthread_mutex_init( &mbox->lock_free,  &attr);
  pthread_mutex_init( &mbox->lock_inbox, &attr);
  pthread_mutex_init( &mbox->notempty,   &attr);
  mbox->list_free  = NULL;
  mbox->list_inbox = NULL;
  mbox->mbox_ID=node_ID;
  
  DCMflush();
  return mbox;
}

void LpelMailboxDestroy( mailbox_t *mbox)
{
  mailbox_node_t *node;

  assert( mbox->list_inbox == NULL);
  /* free all free nodes */
  DCMflush();
  while(pthread_mutex_trylock(&mbox->lock_free) != 0){
    DCMflush();
  }
  while (&mbox->list_free != NULL) {
    /* pop free node off */
    node = mbox->list_free;
    mbox->list_free = node->next; /* can be NULL */
    /* free the memory for the node */
    free( node);
  }
  pthread_mutex_unlock( &mbox->lock_free);
  DCMflush();

  /* destroy sync primitives */
  pthread_mutex_destroy( &mbox->lock_free);
  pthread_mutex_destroy( &mbox->lock_inbox);
  pthread_mutex_destroy( &mbox->notempty);

  free(mbox);
  
  /* destroy an attribute */
  pthread_mutexattr_destroy(&attr);
}

void LpelMailboxSend( mailbox_t *mbox, workermsg_t *msg)
{
  if (mbox->mbox_ID == master_ID) {
    lock(mbox->mbox_ID);
  } else {
    DCMflush();
    while(pthread_mutex_trylock(&mbox->lock_inbox) != 0){
      DCMflush();
    }
    DCMflush();
  }
  /* get a free node from recepient */
  mailbox_node_t *node = GetFree( mbox);

  /* copy the message */
  node->msg = *msg;
  DCMflush();
  /* put node into inbox */
  if ( mbox->list_inbox == NULL) {
    /* list is empty */
    mbox->list_inbox = node;
    node->next = node; /* self-loop */
  } else {
    /* insert stream between last node=list_inbox
       and first node=list_inbox->next */
    node->next = mbox->list_inbox->next;
    mbox->list_inbox->next = node;
    mbox->list_inbox = node;
  }

  if (mbox->mbox_ID == master_ID) {
    unlock(mbox->mbox_ID);
  } else {
    pthread_mutex_unlock( &mbox->lock_inbox);
  }
  DCMflush();
}


void LpelMailboxRecv( mailbox_t *mbox, workermsg_t *msg)
{
  mailbox_node_t *node;
  bool message=false;
  bool go_on=false;

  while(go_on==false){
    if(mbox->list_inbox != NULL){
      if (mbox->mbox_ID == master_ID){
        lock(mbox->mbox_ID);
      }else{
        DCMflush();
      	while(pthread_mutex_trylock(&mbox->lock_inbox) != 0){
      	  DCMflush();
      	}
      	DCMflush();
      }
      go_on=true;
    }
  }
  
  assert( mbox->list_inbox != NULL);

  /* get first node (handle points to last) */
  node = mbox->list_inbox->next;
  if ( node == mbox->list_inbox) {
    /* self-loop, just single node */
    mbox->list_inbox = NULL;
  } else {
    mbox->list_inbox->next = node->next;
  }
  pthread_mutex_unlock( &mbox->lock_inbox);

  /* copy the message */
  *msg = node->msg;

  /* put node into free pool */
  PutFree( mbox, node);
  DCMflush();
  if (mbox->mbox_ID == master_ID){
    unlock(mbox->mbox_ID);
  }else{
    pthread_mutex_unlock(&mbox->lock_inbox);
    DCMflush();
  }
}

/**
 * @return 1 if there is an incoming msg, 0 otherwise
 * @note: does not need to be locked as a 'missed' msg
 *        will be eventually fetched in the next worker loop
 */
int LpelMailboxHasIncoming( mailbox_t *mbox)
{
  DCMflush();
  return ( mbox->list_inbox != NULL);
}