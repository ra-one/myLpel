#include <stdlib.h>
#include "placementscheduler.h"
#include "scheduler.h"
#include "worker.h"


typedef struct lpel_worker_indices {
  int * workers;
  int n;
} lpel_worker_indices_t;

lpel_worker_indices_t task_types[SCHED_NUM_PRIO];


void LpelPlacementSchedulerDestroy()
{
  int i;
  for(i = 0; i<SCHED_NUM_PRIO; i++) {
    if(task_types[i].n > 0) {
      free(task_types[i].workers);
    }
  }
}

void LpelPlacementSchedulerInit()
{
  int i;
  int number_workers = LpelWorkerNumber();

#ifdef TASK_WORKER_SEPARATION
  assert(SCHED_NUM_PRIO >= 2);
  assert(number_workers >= 2);
  int num0_workers = (number_workers/6 > 0) ? number_workers/6 : 1;
  int num1_workers = number_workers-num0_workers;
  int num0_i;
  int num1_i;

  task_types[0].workers = malloc(num0_workers * sizeof(int));
  task_types[0].n = num0_workers;
  task_types[1].workers = malloc(num1_workers * sizeof(int));
  task_types[1].n = num1_workers;

  for(i = 0, num0_i = 0, num1_i = 0; i < number_workers; i++) {
    if(i % num0_workers == 0) {
      task_types[0].workers[num0_i] = i;
      num0_i++;
    } else {
      task_types[1].workers[num1_i] = i;
      num1_i++;
    }
  }

  for(i = 2; i<SCHED_NUM_PRIO; i++) {
    task_types[i].n = 0;
  }
#else
  assert(SCHED_NUM_PRIO >= 1);
  task_types[0].workers = malloc(number_workers * sizeof(int));
  task_types[0].n = number_workers;
  for(i = 0; i < task_types[0].n; i++) {
    task_types[0].workers[i] = i;
  }

  for(i = 1; i<SCHED_NUM_PRIO; i++) {
    task_types[i].n = 0;
  }
#endif

  if(number_workers >= 2) {
    LpelTaskCreate(number_workers,
                   0,
                   &LpelPlacementSchedulerRun,
                   NULL,
                   256 * 1024);
  }

}

void LpelPlacementSchedulerWorkerIndices(int prio, int **workers, int *n)
{
#ifdef TASK_WORKER_SEPARATION
  assert(prio < 2);
  *workers = task_types[prio].workers;
  *n = task_types[prio].n;
#else
  *workers = task_types[0].workers;
  *n = task_types[0].n;
#endif
}

void * LpelPlacementSchedulerRun(void * args)
{
/*  while(LpelTaskIterHasNext(iter)) {
    lpel_task_t *t;
    int current_worker;
    double c;
    int migrate;
#ifdef TASK_WORKER_SEPARATION
    int prio;
#endif

    t = LpelTaskIterNext(iter);
    current_worker = t->current_worker;

    c = (double)rand() / (double)RAND_MAX;
    migrate = (c < 0.5) ? 1 : 0;
#ifdef TASK_WORKER_SEPARATION
    prio = LpelTaskGetPrio(t);
    if(prio == 2) {
      t->new_worker = c ? control.worker_list[(rand() % control.n)] : current_worker;
    } else {
      t->new_worker = c ? other.worker_list[(rand() % other.n)] : current_worker;
    }
#else
    t->new_worker = c ? rand() % number_of_workers : current_worker;
#endif
  }
  LpelTaskIterDestroy(iter);*/
}

