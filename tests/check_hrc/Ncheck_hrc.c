#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "hrc_lpel.h"

#include "scc.h"
#define malloc SCCMallocPtr
#define free SCCFreePtr

int id_counter = 100;

typedef struct {
  lpel_stream_t *in, *out;
  int id;
} channels_t;


void *counter(void *arg){
  int i,j=1;
  if(j){
    for(i=0;i<10;i++){printf("%d\t",i);}
    printf("\n\n");
    fflush(stdout);
    j = 0;
  }
  return NULL;
}


void *Relay(void *inarg)
{
  lpel_task_t *t;
  channels_t *ch = (channels_t *)inarg;
  int term = 0;
  int id = ch->id;
  char *item;
  lpel_stream_desc_t *in, *out;

  in = LpelStreamOpen(ch->in, 'r');
  out = LpelStreamOpen(ch->out, 'w');

  printf("\nRelay START, read from %d, write to %d\n\n", LpelStreamGetId(in), LpelStreamGetId(out));

  while (!term) {
    item = LpelStreamRead( in);
    assert( item != NULL );
    //printf("Relay %d: %s", id, item );
    if ( 0 == strcmp( item, "T\n")) {
      term = 1;
      printf("relay id %d term forwarded\n",id);
      LpelStreamWrite( out, item);
    } else if(0 == strcmp( item, "D\n")){
      t = LpelTaskCreate( 0, counter, NULL, 8192);
      int *id = (int*) t + 2;
      *id = id_counter++;
      printf("dynamic task creat\n");
      LpelTaskStart(t);
      printf("dynamic task created\n");
    } else
      LpelStreamWrite( out, item);
  } // end while
  LpelStreamClose( in, 1);
  LpelStreamClose( out, 0);
  free(ch);
  printf("Relay %d TERM\n", id);
  return NULL;
}


static channels_t *ChannelsCreate(lpel_stream_t *in, lpel_stream_t *out, int id)
{
  channels_t *ch = (channels_t *) SCCMallocPtr( sizeof(channels_t));
  ch->in = in;
  ch->out = out;
  ch->id = id;
  return ch;
}

lpel_stream_t *PipeElement(lpel_stream_t *in, int depth)
{
  lpel_stream_t *out;
  channels_t *ch;
  lpel_task_t *t;
  int wid = depth % 2;
  mon_task_t *mt;

  out = LpelStreamCreate(0);
  ch = ChannelsCreate( in, out, depth);
  t = LpelTaskCreate( 0, Relay, ch, 8192);
  //mt = LpelMonTaskCreate(LpelTaskGetId(t), NULL);
  //LpelTaskMonitor(t, mt);
  LpelTaskStart(t);

  printf("Created Relay %d\n", depth );
  return (depth > 0) ? PipeElement( out, depth-1) : out;
}



static void *Outputter(void *arg)
{
  lpel_stream_desc_t *in = LpelStreamOpen((lpel_stream_t*)arg, 'r'); 
  char *item;
  int term = 0;

  printf("Outputter START\n");

  while (!term) {
    item = LpelStreamRead(in);
    assert( item != NULL );
    //printf("Out: %s", item );
    printf("\n\n*************************************\nOut: %s\n*************************************\n", item );

    if ( 0 == strcmp( item, "T\n")) {
      term = 1;
    }
    free( item);
  } // end while

  LpelStreamClose( in, 1);
  printf("Outputter TERM\n");

  LpelStop();
  return NULL;
}


static void *Inputter(void *arg)
{
  lpel_stream_desc_t *out = LpelStreamOpen((lpel_stream_t*)arg, 'w'); 
  char *buf;

  printf("Inputter START\n");
  do {
    buf = fgets( malloc( 120 * sizeof(char) ), 119, stdin  );
    printf("inputter read %s\n", buf);
    if(0 == strcmp(buf, "D\n") || 0 == strcmp(buf, "T\n")){
      LpelStreamWrite( out, buf);
    } else {
    	LpelStreamWrite( out, strcpy( malloc(120 * sizeof(char)), buf));
    	LpelStreamWrite( out, strcpy( malloc(120 * sizeof(char)), buf));
    	LpelStreamWrite( out, strcpy( malloc(120 * sizeof(char)), buf));
    	LpelStreamWrite( out, strcpy( malloc(120 * sizeof(char)), buf));
    }
  } while ( 0 != strcmp(buf, "T\n") );

  LpelStreamClose( out, 0);
  printf("Inputter TERM\n");
  return NULL;
}

static void testBasic(void)
{
  lpel_stream_t *in, *out;
  lpel_config_t cfg;
  lpel_task_t *intask, *outtask;
  mon_task_t *mt;

  cfg.num_workers = 2; //number of cores in SCC
  cfg.proc_workers = 2;
  cfg.proc_others = 0;
  cfg.flags = 0;
  cfg.type = HRC_LPEL;

  unsigned long flags = 1 << 7 - 1;
  //LpelMonInit(&cfg.mon, flags);
  SCCInit(0,2);
  LpelInit(&cfg);
  LpelStart(&cfg);
  printf("\n\n*************************************\n\tcalling LpelCleanup\n*************************************\n\n");
  if( SCCIsMaster()) {
    in = LpelStreamCreate(0);
    out = PipeElement(in, 2);
    outtask = LpelTaskCreate( -1, Outputter, out, 8192);
    LpelTaskStart(outtask);
    printf("\n\n*************************************\nOut Task started\n*************************************\n");

    intask = LpelTaskCreate( -1, Inputter, in, 8192);
    LpelTaskStart(intask);
    printf("\n\n*************************************\nIn Task started\n*************************************\n");
  }
  LpelCleanup();
 	printf("\n\n*************************************\n\tcalling LpelMonCleanup\n*************************************\n\n");
  //LpelMonCleanup();
}

int main(void)
{
  testBasic();
  printf("test finished\n");
  return 0;
}
