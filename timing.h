#ifndef _TIMING_H_
#define _TIMING_H_


#define BILLION 1000000000L
/*
 * Link with librt:
 *   -lrt
 */
#include <time.h>

#include "debug.h"

typedef struct timespec timing_t;



/**
 * Start timing
 *
 * Gets current timestamp and stores it in t
 */
static inline void TimingStart(timing_t *t)
{
  (void) clock_gettime(CLOCK_MONOTONIC, t);
  /*TODO check if CLOCK_MONOTONIC is available */
}

/**
 * End timing, store the elapsed time in t
 *
 * Precond: TimingStart() was called on t
 */
static inline void TimingEnd(timing_t *t)
{
  timing_t start, end;
  
  /* get end time  */
  (void) clock_gettime(CLOCK_MONOTONIC, &end);
  /* store start time */
  start = *t;

  /* calculate elapsed time to t,
   * assuming end > start and *.tv_nsec < BILLION
   */
  if (end.tv_nsec < start.tv_nsec) {
    start.tv_nsec -= BILLION;
    start.tv_sec  += 1L;
  }
  t->tv_nsec = end.tv_nsec - start.tv_nsec;
  t->tv_sec  = end.tv_sec  - start.tv_sec;
}

/**
 * Add a time to another
 *
 * Adds val to t, val is not modified
 */
static inline void TimingAdd(timing_t *t, const timing_t *val)
{
  t->tv_sec  += val->tv_sec;
  t->tv_nsec += val->tv_nsec;
  /* normalize */
  if (t->tv_nsec > BILLION) {
    t->tv_nsec -= BILLION;
    t->tv_sec  += 1L;
  }
}

/**
 * Set a time
 */
static inline void TimingSet(timing_t *t, const timing_t *val)
{
  t->tv_sec  = val->tv_sec;
  t->tv_nsec = val->tv_nsec;
}

/**
 * Clear a time to 0
 */
static inline void TimingZero(timing_t *t)
{
  t->tv_sec  = 0;
  t->tv_nsec = 0;
}


static inline double TimingToNSec(const timing_t *t)
{
  return ((double)t->tv_sec)*1000000000.0 + (double)t->tv_nsec;
}


/**
 * Calculate the exponential average
 *
 * Updates t by last with weight factor alpha
 * tnew = last*alpha + thist*(1-alpha)
 * 
 * Precond: alpha in [0,1]
 */
static inline void TimingExpAvg(timing_t *t,
  const timing_t *last, const float alpha)
{
  double dlast, dhist, dnew;

  dlast = TimingToNSec(last);
  dhist = TimingToNSec(t);

  dnew = dlast*alpha + dhist*(1-alpha);

  t->tv_sec = (unsigned long)(dnew/1000000000.0);
  t->tv_nsec  = (unsigned long)(dnew - (double)t->tv_sec*1000000000);
}


/**
 * Get milliseconds from timing
 */
static inline double TimingToMSec(const timing_t *t)
{
  return (((double)t->tv_sec) * 1000.0f) + (((double)t->tv_nsec) / 1000000.0f);
}

#endif /* _TIMING_H_ */
