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


/* Utility functions*/
typedef int bool;
#define false 0
#define true  1

#define FLAG_SET    1
#define FLAG_UNSET  0

typedef int FLAG_STATUS;
typedef t_vcharp FLAG;

/* mailbox structures */

typedef struct mailbox_node_t {
  struct mailbox_node_t *next;
  workermsg_t msg;
} mailbox_node_t;

struct mailbox_t {
  pthread_mutex_t lock_inbox;
  int             mbox_ID;
  mailbox_node_t  *list_free;
  mailbox_node_t  *list_inbox;
};

//#define _USE_MAILBOX_DBG__

#ifdef _USE_MAILBOX_DBG__
#define MAILBOX_DBG printf
#else
#define MAILBOX_DBG	//
#endif

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
    //printf("Mailbox getFree: node->next %p\n",node->next);
    mbox->list_free = node->next; /* can be NULL */
  } else {
    /* allocate new node */
    node = (mailbox_node_t *) malloc( sizeof( mailbox_node_t));
  }
  DCMflush();
  
  //MAILBOX_DBG("\nnode %p\n",node);
  
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
  return (mbox->mbox_ID == 0);
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
    //allmbox[i] = addr+(i*(SHM_MEMORY_SIZE/size))+0x11a0;    
    allmbox[i] = (void*)M_START(i);    
    printf("allmbox[%d] %p\n",i,allmbox[i]);
  }	
}

mailbox_t *LpelMailboxCreate(void)
{
  //nk is not used
  mailbox_t *mboxTemp = malloc(sizeof(mailbox_t));
  mailbox_t *nk,*mbox=mbox_start_addr;
  pthread_mutexattr_t attr;
  
  pthread_mutexattr_init( &attr);
  pthread_mutexattr_setpshared( &attr, PTHREAD_PROCESS_SHARED);
 
  mboxTemp->mbox_ID = node_ID--;
  pthread_mutex_init( &mboxTemp->lock_inbox, &attr);
  mboxTemp->list_free  = NULL;
  mboxTemp->list_inbox = NULL;
  cpy_mailbox_to_mpb((void *)nk, (void *) mboxTemp, sizeof(mailbox_t));
  //MAILBOX_DBG("\n\nstart_addr %p, return of mbox %p\n\n",mbox_start_addr,mbox);
  free(mboxTemp);
  DCMflush();
  return mbox;
}

mailbox_t *LpelMailboxCreateW(void)
{
  //nk is not used
  mailbox_t *mbox = malloc(sizeof(mailbox_t));
  
  pthread_mutexattr_t attr;
  
  pthread_mutexattr_init( &attr);
  pthread_mutexattr_setpshared( &attr, PTHREAD_PROCESS_SHARED);
  
  mbox->mbox_ID = node_ID--;
  pthread_mutex_init( &mbox->lock_inbox, &attr);
  mbox->list_free  = NULL;
  mbox->list_inbox = NULL;
  DCMflush();
  return mbox;
}

void LpelMailboxDestroy( mailbox_t *mbox){}

void LpelMailboxSend( mailbox_t *mbox, workermsg_t *msg)
{
  int value=-1;
  printf("Mailbox Send: mbox->mbox_ID %d, mbox %p\n",mbox->mbox_ID,mbox);
  if (mbox->mbox_ID >= 0) {                                   MAILBOX_DBG("\nMailbox send %d: Master lock\n",mbox->mbox_ID);    
    while(value != AIR_MBOX_SYNCH_VALUE){
		  atomic_incR(&atomic_inc_regs[mbox->mbox_ID],&value);
  	}
  }else{                                                         MAILBOX_DBG("\nMailbox send %d: Worker lock\n",mbox->mbox_ID);
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
  flush();
  /* put node into inbox */
  if ( mbox->list_inbox == NULL) {                               MAILBOX_DBG("\nMailbox send %d: list_inbox is null\n",mbox->mbox_ID);
    /* list is empty */
    mbox->list_inbox = node;
    node->next = node; /* self-loop */
  } else {                                                       MAILBOX_DBG("\nMailbox send %d: list_inbox is NOT null\n",mbox->mbox_ID);
    /* insert stream between last node=list_inbox
       and first node=list_inbox->next */
    node->next = mbox->list_inbox->next;
    mbox->list_inbox->next = node;
    mbox->list_inbox = node;
  }
  FOOL_WRITE_COMBINE;
  
  if (mbox->mbox_ID >= 0) {                                      MAILBOX_DBG("\nMailbox send %d: Master unlock\n",mbox->mbox_ID);
    atomic_writeR(&atomic_inc_regs[mbox->mbox_ID],AIR_MBOX_SYNCH_VALUE);
  } else {                                                       MAILBOX_DBG("\nMailbox send %d: Worker unlock\n",mbox->mbox_ID);
    pthread_mutex_unlock( &mbox->lock_inbox);
  }
  DCMflush();  
}


void LpelMailboxRecv( mailbox_t *mbox, workermsg_t *msg)
{
  mailbox_node_t *node;
  bool message=false;
  bool go_on=false;
  int value=-1;
  printf("Mailbox Recv: mbox->mbox_ID %d, mbox %p\n",mbox->mbox_ID,mbox);
  MAILBOX_DBG("Mailbox Recv: mbox->mbox_ID %d, mbox %p isMaster(t/f, 1/..) %d\n",mbox->mbox_ID,mbox,isMasterMailbox(mbox));
  while(go_on==false){usleep(1);
    flush();
    DCMflush();
    //printf("%d ",mbox->mbox_ID);fflush(stdout);
    
    if(mbox->list_inbox != NULL){                             MAILBOX_DBG("\nMailbox Recv %d: list_inbox is not null\n",mbox->mbox_ID);
      if (mbox->mbox_ID >= 0) {                               MAILBOX_DBG("\nMailbox Recv %d: Master lock\n",mbox->mbox_ID);
        while(value != AIR_MBOX_SYNCH_VALUE){
  				  atomic_incR(&atomic_inc_regs[mbox->mbox_ID],&value);
        }
      }else{                                                     MAILBOX_DBG("\nMailbox Recv %d: Worker lock\n",mbox->mbox_ID);
        DCMflush();
      	while(pthread_mutex_trylock(&mbox->lock_inbox) != 0){
      	  DCMflush();
      	}
      	DCMflush();
      }
      go_on=true;
    }else{
      //printf("\nMailbox %d %p inbox is null,mbox->list_inbox %p\n",mbox->mbox_ID,mbox,mbox->list_inbox);
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
  
  /* copy the message */
  *msg = node->msg;

  /* put node into free pool */
  PutFree( mbox, node);
  FOOL_WRITE_COMBINE;
  DCMflush();

  if (mbox->mbox_ID >= 0) {                                     MAILBOX_DBG("\nMailbox Recv %d: Master unlock\n",mbox->mbox_ID);
    atomic_writeR(&atomic_inc_regs[mbox->mbox_ID],AIR_MBOX_SYNCH_VALUE);
  }else{                                                           MAILBOX_DBG("\nMailbox Recv %d: Worker unlock\n",mbox->mbox_ID);
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
  flush();
  return ( mbox->list_inbox != NULL);
}