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
 * @defgroup varserver varserver
 * @brief RealTime In-Memory Publish/Subscribe Key/Value store
 * @{
 */

/*============================================================================*/
/*!
@file server.c

    Variable Server

    The Variable Server is a real time in-memory pub/sub key/value store
    It is a single threaded, POSIX compliant server application used to
    centrally store key/value data for multiple clients.  It is designed
    for real-time use in embedded systems.

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include "stats.h"
#include "metric.h"
#include "handlers.h"
#include "shmserver.h"
#include "sockserver.h"
#include "blocklist.h"
#include "server.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*==============================================================================
        Private types
==============================================================================*/


/*==============================================================================
        Private function declarations
==============================================================================*/

static int InitStats( void );

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*==============================================================================
        Public function definitions
==============================================================================*/

/*============================================================================*/
/*  main                                                                      */
/*!
    Main entry point for the variable server

    The main function starts the variable server process and waits for
    messages from clients

    @param[in]
        argc
            number of arguments on the command line
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @return none

==============================================================================*/
void main(int argc, char **argv)
{
    int sock;

    /* initialize the varserver statistics */
    InitStats();

    /* initialize the shared memory variable server interface */
    SHMSERVER_Init();

    sock = SOCKSERVER_Init();

    /* loop forever processing signals */
    while(1)
    {
        /* run the socket server */
        SOCKSERVER_Run( sock );
    }
}


/*============================================================================*/
/*  InitStats                                                                 */
/*!
    Initialize the varserver statistics

    The InitStats function creates the varserver statistics variables

    @retval EOK - stats initialized ok
    @retval EINVAL - invalid arguments

==============================================================================*/
static int InitStats( void )
{
    VarInfo info;
    size_t len;
    VAR_HANDLE hVar;

    /* initialize an empty stats object */
    STATS_Initialize();

    /* set the transactions per second variable */
    STATS_SetRequestsPerSecPtr(MakeMetric("/varserver/stats/tps" ));
    STATS_SetTotalRequestsPtr(MakeMetric("/varserver/stats/transactions"));

    /* set up the blocked client counter metric */
    SetBlockedClientMetric(MakeMetric("/varserver/stats/blocked_clients"));

    /* set up render handler for client info */
    hVar = MakeStringMetric("/varserver/client/info", BUFSIZ);

    AddRenderHandler( hVar, PrintClientInfo );

    /* set up metrics to track handler function invocations */
    InitHandlerMetrics();

    return EOK;
}


/*! @}
 * end of varserver group */
