#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "desBoxes.h"
#include <string.h>
#include <time.h>

#include "information.h"

observer_t *obs;

int SIZE;  //size of message to encrypt
int MESS;  // number of message to generate
int NODES; // number of pipelines

//num_node 1 will be one branch of pipeline
/************ SOURCE SINK ***************/
void lpel_source(lpel_stream_t **sourceOut) {
  int node;
  lpel_stream_desc_t **output;

  fprintf(stderr,"================================\n\tSOURCE start\n================================\n");
  
  printf("NODES %d, MESS %d, SIZE %d, obs %p\n",NODES,MESS,SIZE,obs);
  output = SCCMallocPtr(sizeof(lpel_stream_desc_t*) * NODES);
  for(node=0; node < NODES; node++){
    output[node] = LpelStreamOpen(sourceOut[node], 'w');
  }

  char pt[8] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
  char key[7] = {'1', '2', '3', '4', '5', '6', '7'};
  int i, j;
  char *d;
  char *p;
  char *k;
  for (i = 0; i < MESS; i++) {
    d = (char *) SCCMallocPtr(SIZE * (8 + 7) + 1);
    *d = 'E';
    p = d + 1;
    //k = p + (SIZE * 7);
    k = p + (SIZE * 8);
    for (j = 0; j < SIZE; j++) {
      //memcpy(&p[j * 8], &pt[0], 8);
      //memcpy(&k[j * 7], &key[0], 7);
    }
    printf("\tSOSI mess no %d\n",i);
    //LpelSdPrint(output[i % NODES]);
    LpelStreamWrite(output[i % NODES], d);
  }
  printf("Total mess generated %d\n",i);

  for(node=0; node< NODES; node++) {
    p = (char *) SCCMallocPtr(sizeof(char));
    //p = (char *) SCCMallocPtr(SIZE * (8 + 7) + 1);
    *p = 'T';
    LpelStreamWrite(output[node], p);
    LpelStreamClose(output[node], 0);
    printf("SRC sent out T msg\n");
  }
  SCCFreePtr(output);
  fprintf(stderr,"================================\n\tSOURCE finish\n================================\n");
}

void lpel_sink(lpel_stream_t **sinkIn) {
 fprintf(stderr,"================================\n\tSINK start\n================================\n");
 
 static int outMess=0;
 int node;
 char *data;
 int *streamOpen;
 
 int so = NODES;
 printf("NODES %d, MESS %d, SIZE %d, obs %p\n",NODES,MESS,SIZE,obs);
 streamOpen = SCCMallocPtr(sizeof(int) * NODES); 
 
 lpel_stream_desc_t **input;
 
 input = SCCMallocPtr(sizeof(lpel_stream_desc_t*) * NODES);
 for(node=0; node < NODES; node++){
    input[node] = LpelStreamOpen(sinkIn[node], 'r');
    streamOpen[node] = 1;
    printf("SOSI: just opened stream %d, streamOpen[%d] %d\n",node,node,streamOpen[node]);
 }
 //static int nk=0;
 while(so != 0){
 //printf("inside while %d\n",nk++);
   for(node=0; node < NODES; node++){
     if(streamOpen[node] == 1){
       data = (char *)LpelStreamRead(input[node]);
       outMess++;
       if(data[0] == 'T') {
         //printf("********************so %d\n",so);
         printf("SINK received T msg for node %d\n",node);
         streamOpen [node] = 0;
         so--;
       }
       printf("SOSI: read from %d free %d so %d\n",node,outMess,so);
       SCCFreePtr(data);      
     }
   }
 }
 printf("After while loop\n");
 for(node=0; node < NODES; node++) LpelStreamClose(input[node], 1);
 SCCFreePtr(streamOpen);
 SCCFreePtr(input);
 LpelStop();
 SNETGLOBWAIT = SNETGLOBWAITVAL;
 fprintf(stderr,"================================\n\tSINK finish %d\n================================\n",outMess);
}

