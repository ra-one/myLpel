/**
 * Main LPEL module
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <errno.h>
#include <sched.h>
#include <unistd.h>  /* sysconf() */
#include <pthread.h> /* worker threads are OS threads */

#include <lpel_common.h>

#include "arch/mctx.h"
#include "lpelcfg.h"    // lpel cfg
#include "lpel_main.h"

#ifdef USE_SCC
#include "scc.h"
#endif /*USE_SCC*/

/**
 * Get the number of available cores
 */
int LpelGetNumCores( int *result)
{
  *result = 1;
  return 0;
}


/**
 * Initialise the LPEL
 *
 *  num_workers, proc_workers > 0
 *  proc_others >= 0
 *
 *
 * EXCLUSIVE: only valid, if
 *       #proc_avail >= proc_workers + proc_others &&
 *       proc_others != 0 &&
 *       num_workers == proc_workers
 *
 */
void LpelInit(lpel_config_t *cfg)
{
  /* store a local copy of cfg */
  _lpel_global_config = *cfg;

  #ifdef USE_MCTX_PCL
  int res = co_thread_init();
  /* initialize machine context for main thread */
  assert( 0 == res);
#endif
}


int LpelStart(lpel_config_t *cfg)
{
  int res;
 
  /* initialise workers */
  LpelWorkersInit( cfg);

  LpelWorkersSpawn();
   
  return 0;
}

void LpelStop(void)
{
  LpelWorkersTerminate();
}



/**
 * Cleans the LPEL up
 * - wait for the workers to finish
 * - free the data structures of worker threads
 */
void LpelCleanup(void)
{
  /* Cleanup workers */
  LpelWorkersCleanup();

#ifdef USE_MCTX_PCL
  /* cleanup machine context for main thread */
  co_thread_cleanup();
#endif
}





