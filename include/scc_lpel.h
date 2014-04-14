#ifndef _SCC_LPEL_H
#define _SCC_LPEL_H_

#ifdef USE_SCC
#include "scc.h"
#include "/shared/nil/snetInstall/include/debugging.h"
//#define malloc SCCMallocPtr
//#define valloc SCCMallocPtr
//#define free SCCFreePtr
#endif /*USE_SCC*/

//called by sosi
void decreaseFrequency();
void increaseFrequency();

#endif /* _SCC_LPEL_H_ */
