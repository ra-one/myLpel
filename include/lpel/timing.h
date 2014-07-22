#ifndef _TIMING_H_
#define _TIMING_H_

#include "scc.h"

typedef timespecSCC lpel_timing_t;

#define LPEL_TIMING_INITIALIZER  {0,0}

void LpelTimingIni(lpel_timing_t *t);
void LpelTimingNow(lpel_timing_t *t);
void LpelTimingStart(lpel_timing_t *t);
void LpelTimingEnd(lpel_timing_t *t);
void LpelTimingAdd(lpel_timing_t *t, const lpel_timing_t *val);
void LpelTimingDiff( lpel_timing_t *res, const lpel_timing_t *start, const lpel_timing_t *end);
void LpelTimingSet(lpel_timing_t *t, const lpel_timing_t *val);
void LpelTimingZero(lpel_timing_t *t);
int LpelTimingEquals(const lpel_timing_t *t1, const lpel_timing_t *t2);
double LpelTimingToNSec(const lpel_timing_t *t);
double LpelTimingToMSec(const lpel_timing_t *t);
void LpelTimingExpAvg(lpel_timing_t *t, const lpel_timing_t *last, const float alpha);


#endif /* _TIMING_H_ */
