/*==============================================================================
MIT License

Copyright (c) 2025

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
#include <sys/mman.h>
#include <fcntl.h>
#include "gc.h"
#include "stats.h"

/*==============================================================================
        Private function declarations
==============================================================================*/
static void CreateGCTimer( int timeoutms );

/*==============================================================================
        Public function definitions
==============================================================================*/

void GC_Initialize(void)
{
    /* 10 seconds */
    CreateGCTimer( 10000 );
}

/* Determine if a PID is alive: kill(pid,0) returns 0 if exists or -1 with ESRCH if not */
static int pid_is_alive(pid_t pid)
{
    if (pid <= 0) return 0;
    if (kill(pid, 0) == 0) return 1;
    return (errno == EPERM); /* exists but no permission */
}

void GC_Process( VarClient **table, size_t max_clients )
{
    size_t i;
    if (!table || max_clients == 0) return;

    for ( i = 1; i < max_clients; ++i )
    {
        VarClient *p = table[i];
        if (!p) continue;

        const pid_t client_pid = p->client_pid;

        if (!pid_is_alive(client_pid))
        {
            /* stale entry: remove shared object and clear table slot */
            char clientname[64];
            snprintf(clientname, sizeof(clientname), "/varclient_%d", client_pid);

            /* Attempt to unlink shared memory object */
            shm_unlink(clientname);

            /* Unmap the client mapping if this process mapped it */
            munmap(p, sizeof(VarClient));

            table[i] = NULL;
            fprintf(stderr, "varserver: GC removed stale client pid=%d slot=%zu\n", client_pid, i);
            STATS_IncrementGCCleaned();
        }
    }
}

/*============================================================================*/
/*  CreateGCTimer                                                             */
/*!
    Create a repeating GC timer

    @param[in]
        timeoutms
            timer interval in milliseconds

==============================================================================*/
static void CreateGCTimer( int timeoutms )
{
    struct sigevent te;
    struct itimerspec its;
    long secs;
    long msecs;
    timer_t timerID;

    secs = timeoutms / 1000;
    msecs = timeoutms % 1000;

    /* Set and enable alarm */
    te.sigev_notify = SIGEV_SIGNAL;
    te.sigev_signo = SIG_GC_TIMER;
    te.sigev_value.sival_int = 1;
    timer_create(CLOCK_REALTIME, &te, &timerID);

    its.it_interval.tv_sec = secs;
    its.it_interval.tv_nsec = msecs * 1000000L;
    its.it_value.tv_sec = secs;
    its.it_value.tv_nsec = msecs * 1000000L;
    timer_settime(timerID, 0, &its, NULL);
}
