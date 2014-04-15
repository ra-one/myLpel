#ifndef _INFORMATION_H
#define _INFORMATION_H

#include "/shared/nil/snetInstall/include/hrc_lpel.h"
#include "/shared/nil/snetInstall/include/scc.h"

//extern observer_t *obs;

extern int SIZE;  //size of message to encrypt
extern int MESS;  // number of message to generate
extern int NODES; // number of pipelines

typedef struct {
  lpel_stream_t *in, *out;
  int id;
} channel_t;

#endif 
