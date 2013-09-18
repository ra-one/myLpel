#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include "mailbox.h"
#include "hrc_lpel.h"
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

//#define _USE_MAILBOX_DBG__

#ifdef _USE_MAILBOX_DBG__
#define MAILBOX_DBG printf
#else
#define MAILBOX_DBG	//
#endif

//#define DCMflush(); //

#define printf //

/******************************************************************************/
/* Free node pool management functions                                        */
/******************************************************************************/

static mailbox_node_t *GetFree( mailbox_t *mbox)
{
  mailbox_node_t *node;
  node = (mailbox_node_t *) malloc( sizeof( mailbox_node_t));
  return node;
}
static mailbox_node_t *GetFree1( mailbox_t *mbox)
{
  mailbox_node_t *node;

  DCMflush();
  if (mbox->list_free != NULL) {
    /* pop free node off */
    node = mbox->list_free;
    mbox->list_free = node->next; /* can be NULL */
  } else {
    /* allocate new node */
    node = (mailbox_node_t *) malloc( sizeof( mailbox_node_t));
  }
  DCMflush();
  
  MAILBOX_DBG("\nnode %p\n",node);
  
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

static int isMasterMailbox( mailbox_t *mbox)
{
  //MAILBOX_DBG("allmbox[0] %p mbox %p\n",allmbox[0],mbox);
  return !(mbox - allmbox[0]);
}

/******************************************************************************/
/* Public functions                                                           */
/******************************************************************************/

void LpelMailboxInit(int node_id_num, int num_worker){
  node_ID = node_id_num;
  size = num_worker+1; //+1 to include master	
  int i;
  allmbox = (mailbox_t**) malloc(sizeof(mailbox_t*)*size);
  for (i=0; i < size;i++){
    allmbox[i] = addr+(i*(SHM_MEMORY_SIZE/size))+0x11a0;    
    printf("allmbox[%d] %p\n",i,allmbox[i]);
  }	
}

mailbox_t *LpelMailboxCreate(void)
{
  //mailbox_t *mbox = (mailbox_t *) malloc(sizeof(mailbox_t));
  mailbox_t *mbox = malloc(sizeof(mailbox_t));

  pthread_mutexattr_init( &attr);
  pthread_mutexattr_setpshared( &attr, PTHREAD_PROCESS_SHARED);
  
  pthread_mutex_init( &mbox->lock_free,  &attr);
  pthread_mutex_init( &mbox->lock_inbox, &attr);
  pthread_mutex_init( &mbox->notempty,   &attr);
  mbox->list_free  = NULL;
  mbox->list_inbox = NULL;
  mbox->mbox_ID=node_ID--;
  mbox->mbox_ID = (mbox->mbox_ID + 48)%48;
  //node_ID = -99;
  MAILBOX_DBG("MAILBOX address: %p mbox_ID %d\n",mbox,mbox->mbox_ID);
  DCMflush();
  printf("my id %d\n\n\n",mbox->mbox_ID);
  return mbox;
}

void LpelMailboxDestroy1( mailbox_t *mbox)
{
  assert( mbox->list_inbox == NULL);
  
  
  /* destroy sync primitives */
  pthread_mutex_destroy( &mbox->lock_free);
  pthread_mutex_destroy( &mbox->lock_inbox);
  pthread_mutex_destroy( &mbox->notempty);

  free(mbox);
  
  /* destroy an attribute */
  pthread_mutexattr_destroy(&attr);
}

void LpelMailboxDestroy( mailbox_t *mbox)
{
  mailbox_node_t *node;

  assert( mbox->list_inbox == NULL);
  /* free all free nodes */
  DCMflush();/*
  while(pthread_mutex_trylock(&mbox->lock_free) != 0){
    DCMflush();
  }*/
  
  while (mbox->list_free != NULL) {
    /* pop free node off */
    node = mbox->list_free;  
    mbox->list_free = node->next; /* can be NULL */
    /* free the memory for the node */
    free( node);   
    DCMflush();
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

void printListInbox(char* c, mailbox_t *mbox){
  mailbox_node_t *node;
  printf("%s at %f: mbox %p, id %d , mbox->list_inbox = ",c,SCCGetTime(),mbox,mbox->mbox_ID);
  if(mbox->list_inbox == NULL){
    printf("NULL\n");
  }else{
    node =  mbox->list_inbox;
    do{
    printf("%p-> ",node);
    node = node->next;
    } while(node != mbox->list_inbox);    
  }
  printf("\n");
}


void LpelMailboxSend( mailbox_t *mbox, workermsg_t *msg)
{
  int value=-1,pFlag = 1;
  MAILBOX_DBG("Mailbox Send: mbox->mbox_ID %d, mbox %p isMaster(t/f, 1/..) %d\n",mbox->mbox_ID,mbox,isMasterMailbox(mbox));
  while(value != 0){
		  atomic_incR(&atomic_inc_regs[mbox->mbox_ID],&value);
      if(pFlag) {pFlag=0;printf("mailbox send to %d: Inside Wait\n",mbox->mbox_ID);}
  }
  MAILBOX_DBG("\nMailbox send: lock\n");
    
  /* get a free node from recepient */
  mailbox_node_t *node = GetFree( mbox);

  /* copy the message */
  node->msg = *msg;
  DCMflush();
  printListInbox("Send",mbox);
  /* put node into inbox */
  if ( mbox->list_inbox == NULL) {
    MAILBOX_DBG("\nMailbox send: list_inbox is null\n");
    /* list is empty */
    mbox->list_inbox = node;
    node->next = node; /* self-loop */
  } else {
    MAILBOX_DBG("\nMailbox send: list_inbox is NOT null\n");
    /* insert stream between last node=list_inbox
       and first node=list_inbox->next */
    node->next = mbox->list_inbox->next;
    mbox->list_inbox->next = node;
    mbox->list_inbox = node;
  }
  printListInbox("Send",mbox);
  DCMflush();
  MAILBOX_DBG("\nMailbox send: unlock\n");
  atomic_writeR(&atomic_inc_regs[mbox->mbox_ID],0);
}


void LpelMailboxRecv( mailbox_t *mbox, workermsg_t *msg)
{
  mailbox_node_t *node;
  bool message=false;
  bool go_on=false;
  int value=-1,pFlag = 1;
  MAILBOX_DBG("Mailbox Recv: mbox->mbox_ID %d, mbox %p isMaster(t/f, 1/..) %d\n",mbox->mbox_ID,mbox,isMasterMailbox(mbox));
  while(go_on==false){//sleep(1);
    DCMflush();
    //printf(".");fflush(stdout);
    if(mbox->list_inbox != NULL){        
      	while(value != 0){
  				  atomic_incR(&atomic_inc_regs[mbox->mbox_ID],&value);
            if(pFlag) {pFlag=0;printf("mailbox recv Master/worker: %d Inside Wait\n",mbox->mbox_ID);}
        }      	
        MAILBOX_DBG("\nMailbox recv: lock\n");
        go_on=true;
    }//else{printf("\nMailbox %d %p inbox is null,mbox->list_inbox %p\n",mbox->mbox_ID,mbox,mbox->list_inbox);sleep(1);}
  }
  DCMflush();
  assert( mbox->list_inbox != NULL);
  printListInbox("Recv",mbox);
  /* get first node (handle points to last) */
  node = mbox->list_inbox->next;
  if ( node == mbox->list_inbox) {
    /* self-loop, just single node */
    mbox->list_inbox = NULL;
  } else {
    mbox->list_inbox->next = node->next;
  }
  //pthread_mutex_unlock( &mbox->lock_inbox);

  /* copy the message */
  *msg = node->msg;
  
  /* put node into free pool */
  PutFree( mbox, node);
  DCMflush();
  printListInbox("Recv",mbox);
  
  DCMflush();
  MAILBOX_DBG("\nMailbox recv: unlock\n");
  atomic_writeR(&atomic_inc_regs[mbox->mbox_ID],0);
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