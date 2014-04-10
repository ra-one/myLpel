#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include "hrc_lpel.h"

#include "scc.h"

#define PIPE_DEPTH 26 /* min 3*/
#define NUM_WORKERS 13 /* including master*/
#define MSG_TERM ((void*)-1)

#ifndef NUM_MSGS
#define NUM_MSGS 1000
#endif

typedef struct {
  lpel_stream_t *in, *out;
  int id;
} channels_t;

static channels_t *ChannelsCreate(lpel_stream_t *in, lpel_stream_t *out, int id)
{
  channels_t *ch = (channels_t *) SCCMallocPtr( sizeof(channels_t));
  ch->in = in;
  ch->out = out;
  ch->id = id;
  return ch;
}

void *Source(void *inarg)
{
  unsigned long cnt = 0;
  lpel_stream_desc_t *out = LpelStreamOpen((lpel_stream_t *)inarg, 'w');;
  void *item;

  while( cnt < (NUM_MSGS-1) ) {
    item = (void*)(0x10000000 + cnt);
    LpelStreamWrite( out, item);
    cnt++;
  }

  item = MSG_TERM;
  LpelStreamWrite( out, item);

  LpelStreamClose( out, 0);
  printf("Source TERM MESS %lu\n",cnt);
  return NULL;
}



void *Sink(void *inarg)
{
  unsigned long cnt = 0;
  lpel_stream_desc_t *in = LpelStreamOpen((lpel_stream_t *)inarg, 'r');;
  void *item;
  int term = 0;

  while(!term) {
    item = LpelStreamRead( in);
    cnt++;
    if (item==MSG_TERM) term = 1;
    //SCCFreePtr( item);
  }

  LpelStreamClose( in, 1);
  LpelStop();
  SNETGLOBWAIT = SNETGLOBWAITVAL;
  printf("Sink TERM MESS %lu\n",cnt);
  return NULL;
}


void *Relay(void *inarg)
{
  lpel_task_t *t;
  channels_t *ch = (channels_t *)inarg;
  int term = 0;
  void *item;
  lpel_stream_desc_t *in, *out;
  
  in = LpelStreamOpen(ch->in, 'r');
  out = LpelStreamOpen(ch->out, 'w');

  while(!term) {
    item = LpelStreamRead( in);
    if (item==MSG_TERM) term = 1;
    LpelStreamWrite( out, item);
  }

  LpelStreamClose( in, 1);
  LpelStreamClose( out, 0);

  SCCFreePtr(ch);

  return NULL;
}

lpel_stream_t *PipeElement(lpel_stream_t *in, int depth)
{
  lpel_stream_t *out;
  channels_t *ch;
  lpel_task_t *t;

  out = LpelStreamCreate(0);
  ch = ChannelsCreate( in, out, depth);
  
  t = LpelTaskCreate( 0, Relay, ch, 8192);

  LpelTaskStart(t);
  return (depth > 0) ? PipeElement( out, depth-1) : out;
}

static void CreatePipe(void)
{
  lpel_stream_t *glob_in, *glob_out;
  lpel_task_t *tsource, *tsink;


  glob_in = LpelStreamCreate(0);
  glob_out = PipeElement(glob_in, PIPE_DEPTH);

  tsource = LpelTaskCreate( -1, Source, glob_in, 8192);
  printf("\n\n*************************************\nSource Task created\n*************************************\n");
  LpelTaskStart(tsource);

  tsink = LpelTaskCreate( -1, Sink, glob_out, 8192);
  printf("\n\n*************************************\nSink Task created\n*************************************\n");
  LpelTaskStart(tsink);
}



static void testBasic(void)
{
  lpel_config_t cfg;
  memset(&cfg, 0, sizeof(lpel_config_t));

  cfg.num_workers = NUM_WORKERS; //number of cores in SCC to work as worker including master
  cfg.proc_workers = 2;
  cfg.proc_others = 0;
  cfg.flags = 0;
  cfg.type = HRC_LPEL;
  cfg.wait_window_size = 10;
  cfg.wait_threshold = 20;
  
  SCCInit(cfg.num_workers,2,0,"/shared/nil/lpel.host");
  LpelInit(&cfg);

  LpelStart(&cfg);
  if( SCCIsMaster()) {
    CreatePipe();
  }
  LpelCleanup();
 	printf("\n\n*************************************\n\tcalling LpelMonCleanup\n*************************************\n\n");
  SCCStop();
}

int main(void)
{
  testBasic();
  fprintf(stderr,"test finished\n");
  return 0;

}