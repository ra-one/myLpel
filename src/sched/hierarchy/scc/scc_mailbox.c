#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include "mailbox.h"
#include "hrc_lpel.h"
#include "scc.h"


extern uintptr_t  addr;
extern uintptr_t *allMbox;
extern int num_mailboxes;
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
  pthread_mutex_t     lock_inbox;
  mailbox_node_t      *list_free;
  mailbox_node_t      *list_inbox;
  int                 mbox_ID;
};



//#define _USE_MBX_DBG__

#ifdef _USE_MBX_DBG__
#define MAILBOX_DBG_LOCK printf
#define PFLAG 1
#define MAILBOX_DBG	//
#else
#define PFLAG 0
#define MAILBOX_DBG_LOCK //
#define MAILBOX_DBG	//
#endif


#define DCMflush(); //

// 0 false, 1 true
#define USE_TSR 0 
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
    node = (mailbox_node_t *) malloc( sizeof( mailbox_node_t));
  }
  DCMflush();
  
  node->next = NULL;
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

mailbox_t *LpelMailboxCreate(void)
{
 
  mailbox_t *mbox = allMbox[SCCGetNodeRank()];

  pthread_mutexattr_init( &attr);
  pthread_mutexattr_setpshared( &attr, PTHREAD_PROCESS_SHARED);
  
  pthread_mutex_init( &mbox->lock_inbox, &attr);
  mbox->list_free  = NULL;
  mbox->list_inbox = NULL;
  mbox->mbox_ID = SCCGetNodeID();
  DCMflush();
  return mbox;
}

void LpelMailboxDestroy( mailbox_t *mbox)
{
  if (mbox == NULL || mbox->list_inbox != NULL ) return;
  mailbox_node_t *node;

  assert( mbox->list_inbox == NULL);
  /* free all free nodes */
  DCMflush();
  
  while (mbox->list_free != NULL) {
    /* pop free node off */
    node = mbox->list_free;  
    mbox->list_free = node->next; /* can be NULL */
    /* free the memory for the node */
    free( node);   
    DCMflush();
  }
  DCMflush();

  /* destroy sync primitives */
  pthread_mutex_destroy( &mbox->lock_inbox);

  //free(mbox); // as mbox is not allocated
  
  /* destroy an attribute */
  pthread_mutexattr_destroy(&attr);
}

void printListInbox(char* c, mailbox_t *mbox){}
void printListInbox2(char* c, mailbox_t *mbox){
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
}
void printListInbox1(char* c, mailbox_t *mbox){
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
  if(USE_TSR){
    lock((mbox->mbox_ID)+20);
  } else {
    int value=-1,pFlag=1,dbgValue = -1;
    while(value != 0){
      atomic_incR(&atomic_inc_regs[mbox->mbox_ID],&value);
      if(pFlag && value != 0) {
        pFlag=0;
        atomic_readR(&atomic_inc_regs[mbox->mbox_ID],&dbgValue);
        MAILBOX_DBG_LOCK("\nmailbox send to %d: Inside Wait, value: %d, reading %d\n",mbox->mbox_ID,value,dbgValue); 
      }
    }
  }
  
  MAILBOX_DBG_LOCK("Mailbox send: locked %d at %f\n",mbox->mbox_ID,SCCGetTime());
    
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
    //mbox->list_inbox = node;
    //node->next = node; /* self-loop */
    node->next = NULL;
    mbox->list_inbox = node;
  } else {
    MAILBOX_DBG("\nMailbox send: list_inbox is NOT null\n");
    /* insert stream between last node=list_inbox
       and first node=list_inbox->next */
    //node->next = mbox->list_inbox->next;
    //mbox->list_inbox->next = node;
    //mbox->list_inbox = node;
    node->next = mbox->list_inbox;
    mbox->list_inbox = node;
  }
  printListInbox("Send",mbox);
  DCMflush();
  MAILBOX_DBG_LOCK("Mailbox send: unlocked %d at %f\n\n",mbox->mbox_ID,SCCGetTime());
  if(USE_TSR){
    unlock((mbox->mbox_ID)+20);
  } else {
    atomic_writeR(&atomic_inc_regs[mbox->mbox_ID],0);
  }
}


void LpelMailboxRecv( mailbox_t *mbox, workermsg_t *msg)
{
  mailbox_node_t *node;
  bool message=false,go_on=false;
  
  while(go_on==false){
    DCMflush();
    if(mbox->list_inbox != NULL){
      if(USE_TSR){
        lock((mbox->mbox_ID)+20);
      } else {
        int value=-1,pFlag=1,dbgValue = -1;
      	while(value != 0){
  		    atomic_incR(&atomic_inc_regs[mbox->mbox_ID],&value);
          if(pFlag && value != 0) {
            pFlag=0;
            atomic_readR(&atomic_inc_regs[mbox->mbox_ID],&dbgValue);
            MAILBOX_DBG_LOCK("\nmailbox recv on %d: Inside Wait, value: %d, reading %d\n",mbox->mbox_ID,value,dbgValue); 
          }
        }
      }      	
      MAILBOX_DBG_LOCK("Mailbox recv: locked %d at %f\n",mbox->mbox_ID,SCCGetTime());
      go_on=true;
    }//else{printf("\nMailbox %d %p inbox is null,mbox->list_inbox %p\n",mbox->mbox_ID,mbox,mbox->list_inbox);sleep(1);}
  }
  DCMflush();
  assert( mbox->list_inbox != NULL);
  printListInbox("Recv",mbox);
  /* get first node (handle points to last) */
  //node = mbox->list_inbox->next;
  node = mbox->list_inbox;
  if (node->next == NULL){
    mbox->list_inbox = NULL;
  } else {
    mbox->list_inbox = node->next;
  }
/*  if ( node == mbox->list_inbox) {
    // self-loop, just single node
    mbox->list_inbox = NULL;
  } else {
    mbox->list_inbox->next = node->next;
  }*/

  /* copy the message */
  *msg = node->msg;
  
  /* put node into free pool */
  PutFree( mbox, node);
  DCMflush();
  printListInbox("Recv",mbox);
  MAILBOX_DBG_LOCK("Mailbox recv: unlocked %d at %f\n\n",mbox->mbox_ID,SCCGetTime());
  DCMflush();
  if(USE_TSR){
    unlock((mbox->mbox_ID)+20);
  } else {
    atomic_writeR(&atomic_inc_regs[mbox->mbox_ID],0);
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
