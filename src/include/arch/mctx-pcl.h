

#include <stdlib.h>
#include <assert.h>
#include <pcl.h>

#ifdef USE_SCC
#include "scc.h"
#endif /*USE_SCC*/

#define DCMflush(); //

typedef coroutine_t mctx_t;


static inline int mctx_create(mctx_t *mctx, void *func, void *arg, char *sk_addr, long sk_size)
{
  *mctx = co_create(func, arg, sk_addr, sk_size);

  return (*mctx != NULL);
}

static inline void mctx_switch(mctx_t *octx, mctx_t *nctx)
{
	//fprintf(stderr,"switch to %p, from %p\n",nctx,octx);
	printf("mctx_switch: switch to %p\n",*nctx);

	(void) co_call(*nctx);
}

static inline void mctx_switchOLD(mctx_t *octx, mctx_t *nctx)
{
#ifdef USE_SCC
  DCMflush();
  (void) co_call(*nctx);
  DCMflush();
#else /*USE_SCC*/
  (void) co_call(*nctx);
#endif /*USE_SCC*/
}



