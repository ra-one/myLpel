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

//#define _USE_MAILBOX_DBG__

#ifdef _USE_MAILBOX_DBG__
#define MAILBOX_DBG PRT_DBG
#else
#define MAILBOX_DBG	//
#endif

//#define DCMflush(); //

#define PRT_DBG printf
#define PRT_DBG1 //
#define MAILBOX_DBG_LOCK printf


/******************************************************************************/
/* Free node pool management functions                                        */
/******************************************************************************/

static mailbox_node_t *GetFree1( mailbox_t *mbox)
{
  mailbox_node_t *node;
  node = (mailbox_node_t *) malloc( sizeof( mailbox_node_t));
  return node;
}
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
  //return !(mbox - allMbox[0]);
  return !((mbox->mbox_ID)-1);
}

/******************************************************************************/
/* Public functions                                                           */
/******************************************************************************/

void LpelMailboxInit(int node_id_num, int num_worker){}

mailbox_t *LpelMailboxCreate(void)
{
 
  mailbox_t *mbox = allMbox[SCCGetNodeRank()];

  pthread_mutexattr_init( &attr);
  pthread_mutexattr_setpshared( &attr, PTHREAD_PROCESS_SHARED);
  
  pthread_mutex_init( &mbox->lock_inbox, &attr);
  mbox->list_free  = NULL;
  mbox->list_inbox = NULL;
  mbox->mbox_ID = SCCGetNodeID();
  PRT_DBG1("mailbox: node_ID %d, mbox->id %d\n",node_ID,mbox->mbox_ID);
  DCMflush();
  return mbox;
}

void LpelMailboxInit1(int node_id_num, int num_worker){
  node_ID = node_id_num;
  size = num_worker+1; //+1 to include master	
}

mailbox_t *LpelMailboxCreate1(void)
{
   int myId = (node_ID + num_mailboxes)%num_mailboxes;
   PRT_DBG1("mailbox: node_ID %d, myId %d\n",node_ID,myId);
  //mailbox_t *mbox = malloc(sizeof(mailbox_t));
  mailbox_t *mbox = allMbox[myId];

  pthread_mutexattr_init( &attr);
  pthread_mutexattr_setpshared( &attr, PTHREAD_PROCESS_SHARED);
  
  pthread_mutex_init( &mbox->lock_inbox, &attr);
  mbox->list_free  = NULL;
  mbox->list_inbox = NULL;
  //mbox->mbox_ID=node_ID--;
  //mbox->mbox_ID = (mbox->mbox_ID + 48)%48;
  //node_ID = -99;
  if(node_ID < 0){
    mbox->mbox_ID = myId + CORES;
  } else {
    mbox->mbox_ID = myId;
  }
  PRT_DBG1("mailbox: node_ID %d, myId %d, mbox->id %d\n",node_ID,myId,mbox->mbox_ID);
  node_ID--;
  DCMflush();
  MAILBOX_DBG("my id %d %p\n\n\n",mbox->mbox_ID,mbox);
  return mbox;
}

void LpelMailboxDestroy( mailbox_t *mbox)
{
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
void printListInbox1(char* c, mailbox_t *mbox){
  mailbox_node_t *node;
  PRT_DBG("%s at %f: mbox %p, id %d , mbox->list_inbox = ",c,SCCGetTime(),mbox,mbox->mbox_ID);
  if(mbox->list_inbox == NULL){
    PRT_DBG("NULL\n");
  }else{
    node =  mbox->list_inbox;
    do{
    PRT_DBG("%p-> ",node);
    node = node->next;
    } while(node != mbox->list_inbox);
  }
  PRT_DBG("\n");
}


void LpelMailboxSend( mailbox_t *mbox, workermsg_t *msg)
{
  int value=-1,pFlag = 1;
  while(value != 0){
		  atomic_incR(&atomic_inc_regs[mbox->mbox_ID],&value);
      if(pFlag) {
        pFlag=0;PRT_DBG("mailbox send to %d: Inside Wait\n",mbox->mbox_ID);
      }
  }
  MAILBOX_DBG_LOCK("\nMailbox send: locked %d at %f\n",mbox->mbox_ID,SCCGetTime());
    
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
  atomic_writeR(&atomic_inc_regs[mbox->mbox_ID],0);
  MAILBOX_DBG_LOCK("Mailbox send: unlocked %d at %f\n",mbox->mbox_ID,SCCGetTime());
}


void LpelMailboxRecv( mailbox_t *mbox, workermsg_t *msg)
{
  mailbox_node_t *node;
  bool message=false,go_on=false;
  int value=-1,pFlag = 1;
  
  while(go_on==false){
    DCMflush();
    if(mbox->list_inbox != NULL){        
      	while(value != 0){
  				  atomic_incR(&atomic_inc_regs[mbox->mbox_ID],&value);
            if(pFlag) {
              pFlag=0;PRT_DBG("mailbox recv on: %d Inside Wait\n",mbox->mbox_ID);
            }
        }      	
        MAILBOX_DBG_LOCK("\nMailbox recv: locked %d at %f\n",mbox->mbox_ID,SCCGetTime());
        go_on=true;
    }//else{PRT_DBG("\nMailbox %d %p inbox is null,mbox->list_inbox %p\n",mbox->mbox_ID,mbox,mbox->list_inbox);sleep(1);}
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

  /* copy the message */
  *msg = node->msg;
  
  /* put node into free pool */
  PutFree( mbox, node);
  DCMflush();
  printListInbox("Recv",mbox);
  
  DCMflush();
  atomic_writeR(&atomic_inc_regs[mbox->mbox_ID],0);
  MAILBOX_DBG_LOCK("Mailbox recv: unlocked %d at %f\n",mbox->mbox_ID,SCCGetTime());
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