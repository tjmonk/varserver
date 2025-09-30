/*==============================================================================
MIT License

Copyright (c) 2023 Trevor Monk

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
==============================================================================*/

/*!
 * @defgroup stats Statistics
 * @brief Variable Server Statistics
 * @{
 */

/*============================================================================*/
/*!
@file stats.c

    Variable Server Statistics

    The Variable Server Statistics module maintains a set of processing
    metrics used to monitor the performance of the
    notifications associated with a variable.

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include "stats.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*! the RequestStats is used to track the total number of requests
    handled by the variable server, as well as the number of
    requests per second */
typedef struct _RequestStats
{
    /*! track the number of requests */
    size_t requestCount;

    /*! Track the number of requests per second */
    uint64_t *pRequestsPerSec;

    /*! pointer to the VarObject containing the totalRequestCount */
    uint64_t *pTotalRequests;

} RequestStats;

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! request statistics */
static RequestStats stats = {0};

/*==============================================================================
        Private function declarations
==============================================================================*/

static void CreateStatsTimer( int timeoutms );

/*==============================================================================
        Public function definitions
==============================================================================*/

/*============================================================================*/
/*  STATS_Initialize                                                          */
/*!
    Initialize the statistics

    The STATS_Initialize function initializes the varserver statistics object
    and the stats processing timer.

==============================================================================*/
void STATS_Initialize( void )
{
    memset( &stats, 0, sizeof(RequestStats));

    /* set up the statistics timer */
    CreateStatsTimer( 1000 );
}

/*============================================================================*/
/*  STATS_IncrementRequestCount                                               */
/*!
    Increment the request counter

    The STATS_IncrementRequestCount function increments the total number of
    requests

==============================================================================*/
void STATS_IncrementRequestCount( void )
{
    stats.requestCount++;

    if ( stats.pTotalRequests != NULL )
    {
        (*stats.pTotalRequests)++;
    }
}

/*============================================================================*/
/*  STATS_SetRequestsPerSecPtr                                                */
/*!
    Set the pointer to the requests per second statistics

    The STATS_SetRequestsPerSecPtr function sets the pointer to the requests
    per second statistic

    @param[in]
        p
            pointer to the storage for the requests per second statistic

==============================================================================*/
void STATS_SetRequestsPerSecPtr( uint64_t *p )
{
    stats.pRequestsPerSec = p;
}

/*============================================================================*/
/*  STATS_SetTotalRequestsPtr                                                 */
/*!
    Set the pointer to the total requests statistics

    The STATS_SetTotalRequestsPtr function sets the pointer to the total
    requests statistic

    @param[in]
        p
            pointer to the storage for the requests per second statistic

==============================================================================*/
void STATS_SetTotalRequestsPtr( uint64_t *p )
{
    stats.pTotalRequests = p;
}

/*============================================================================*/
/*  STATS_Process                                                             */
/*!
    Process the statistics

    The STATS_Process function processes the varserver statistics once
    every timer interval (1 second)

==============================================================================*/
void STATS_Process( void )
{
    /* update the requests per second counter */
    if ( stats.pRequestsPerSec != NULL )
    {
        *(stats.pRequestsPerSec) = stats.requestCount;
    }

    stats.requestCount=0;
}

/*============================================================================*/
/*  CreateStatsTimer                                                          */
/*!
    Create a repeating 1 second timer

    The CreateStatsTimer creates a timer used for periodic statistics
    generation.

    @param[in]
        timeoutms
            timer interval in milliseconds

==============================================================================*/
static void CreateStatsTimer( int timeoutms )
{
    struct sigevent te;
    struct itimerspec its;
    int sigNo = SIG_STATS_TIMER;
    long secs;
    long msecs;
    timer_t timerID;

    secs = timeoutms / 1000;
    msecs = timeoutms % 1000;

    /* Set and enable alarm */
    te.sigev_notify = SIGEV_SIGNAL;
    te.sigev_signo = sigNo;
    te.sigev_value.sival_int = 1;
    timer_create(CLOCK_REALTIME, &te, &timerID);

    its.it_interval.tv_sec = secs;
    its.it_interval.tv_nsec = msecs * 1000000L;
    its.it_value.tv_sec = secs;
    its.it_value.tv_nsec = msecs * 1000000L;
    timer_settime(timerID, 0, &its, NULL);
}

/*! @}
 * end of stats group */
