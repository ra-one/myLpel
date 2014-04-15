#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "information.h"

#include "sosi.h"
#include "desBoxes.h"

observer_t *obs;

int SIZE;  //size of message to encrypt
int MESS;  // number of message to generate
int NODES; // number of pipelines

static channel_t *channelCreate(lpel_stream_t *in, lpel_stream_t *out)
{
  channel_t *ch = (channel_t *) SCCMallocPtr( sizeof(channel_t));
  ch->in = in;
  ch->out = out;
  ch->id = 999;
  return ch;
}

lpel_stream_t *createInitP(lpel_stream_t *in) {
  lpel_stream_t *out = LpelStreamCreate(0);
  channel_t *ch = channelCreate(in, out);
  lpel_task_t *t = LpelTaskCreate(0, initPTask, ch, 8192);
  LpelTaskStart(t);
  return out;
}

lpel_stream_t *createFinalP(lpel_stream_t *in) {
  lpel_stream_t *out = LpelStreamCreate(0);
  channel_t *ch = channelCreate(in, out);
  lpel_task_t *t = LpelTaskCreate(0, finalPTask, ch, 8192);
  LpelTaskStart(t);
  return out;
}


lpel_stream_t *createSubR(lpel_stream_t *in, int round)
{
  lpel_stream_t *out;
  channel_t *ch;
  lpel_task_t *t;

  out = LpelStreamCreate(0);
  ch = channelCreate( in, out);
  ch->id = round;
  t = LpelTaskCreate( 0, subRoundTask, ch, 8192);
  LpelTaskStart(t);

  return out;
}

void createSource(lpel_stream_t **in)
{
  lpel_task_t *t;
  t = LpelTaskCreate( -1, lpel_source, in, 8192);
  LpelTaskStart(t);
}

void createSink(lpel_stream_t **in)
{
  lpel_task_t *t;
  t = LpelTaskCreate( -1, lpel_sink, in, 8192);
  LpelTaskStart(t);
}

lpel_stream_t *createBranch(lpel_stream_t *in)
{
  lpel_stream_t *out;
  int i,sRound = 16;// des always 16 subrounds
  
  out = createInitP(in);
  
  for(i=1;i<=sRound;i++){
    out = createSubR(out,i);
  }
  
  out = createFinalP(out);
  return out;
}

int main(int argc, char **argv){
  FILE *fd;
  char *key = NULL;
  char *value;
  size_t len = 0;
  
  lpel_stream_t **inList, **outList;
  lpel_config_t cfg;
  int node,tmp;
	
	//cfg.num_workers = 4; //number of cores in SCC to work as worker including master
  cfg.proc_workers = 0;
  cfg.proc_others = 0;
  cfg.flags = 0;
  cfg.type = HRC_LPEL;

	fd = fopen (argv[1],"r");

	getline(&key, &len, fd); strtok_r(key, "=", &value); cfg.num_workers=atoi(value);
	getline(&key, &len, fd); strtok_r(key, "=", &value); cfg.num_wrappers=atoi(value);
	getline(&key, &len, fd); strtok_r(key, "=", &value); cfg.enable_dvfs=atoi(value);
	getline(&key, &len, fd); strtok_r(key, "=", &value); cfg.wait_window_size=atoi(value);
	getline(&key, &len, fd); strtok_r(key, "=", &value); cfg.wait_threshold=atoi(value);

	getline(&key, &len, fd); strtok_r(key, "=", &value);
  value[strcspn ( value, "\n" )] = '\0';  

	SCCInit(cfg.num_workers,cfg.num_wrappers,cfg.enable_dvfs,value,"/shared/nil/des_C/output/master.txt");

	LpelInit(&cfg);
  LpelStart(&cfg);
  
  if( SCCIsMaster()) {
  	// init observer structure
  	obs = (observer_t *) SCCMallocPtr(sizeof(observer_t));
  	// copy observer address to MPB so sink can get it
  	memcpy((void*)SOSIADDR, (const void*)&obs, sizeof(observer_t*));
    OBSSET = 9;
    
		getline(&key, &len, fd); strtok_r(key, "=", &value); obs->no_mess=atoi(value);
		getline(&key, &len, fd); strtok_r(key, "=", &value); obs->mess_size=atoi(value);
		getline(&key, &len, fd); strtok_r(key, "=", &value); obs->num_pipeline=atoi(value);
		getline(&key, &len, fd); strtok_r(key, "=", &value); obs->sleep_micro=atoi(value);
		getline(&key, &len, fd); strtok_r(key, "=", &value); obs->change_mess=atoi(value);
		getline(&key, &len, fd); strtok_r(key, "=", &value); obs->change_percent=atoi(value);
		getline(&key, &len, fd); strtok_r(key, "=", &value); obs->window_size=atoi(value);
		getline(&key, &len, fd); strtok_r(key, "=", &value); obs->thresh_hold=atoi(value);
		getline(&key, &len, fd); strtok_r(key, "=", &value); obs->skip_update=atoi(value);
		
    
    SIZE = obs->mess_size;  //size of message to encrypt
    MESS = obs->no_mess;  // number of message to generate
    NODES = obs->num_pipeline; // number of pipelines
    printf("NODES %d, MESS %d, SIZE %d, obs %p\n",NODES,MESS,SIZE,obs);
		obs->skip_count = 0;
		obs->output_index = 0;
		obs->output_rate = 0;
		obs->freq = 800; //default freq
		obs->input_rate = 1000.0 * 1000.0/obs->sleep_micro;   // input rate in message/sec
		obs->output_interval = (double *) SCCMallocPtr(sizeof(double) * obs->window_size);

  	for (tmp = 0; tmp < obs->window_size; tmp++) obs->output_interval[tmp] = 0.0;
 
  	inList = SCCMallocPtr(sizeof(lpel_stream_t*) * NODES);
  	outList = SCCMallocPtr(sizeof(lpel_stream_t*) * NODES);
  
  	for(node=0;node<NODES;node++){
    	inList[node] = LpelStreamCreate(0);
    	outList[node] = createBranch(inList[node]);
  	}
  	createSink(outList);
  	createSource(inList);
  } else {
    while(OBSSET < 5);
    memcpy((void*)&obs, (const void*)SOSIADDR, sizeof(observer_t*));
    SIZE = obs->mess_size;  //size of message to encrypt
    MESS = obs->no_mess;  // number of message to generate
    NODES = obs->num_pipeline; // number of pipelines
  }
  fclose(fd);
  //printf("\n\n*************************************\n\tcalling LpelCleanup\n*************************************\n\n");
  LpelCleanup();
  SCCStop();
  return 0;
}
