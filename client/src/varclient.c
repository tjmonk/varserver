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
 * @defgroup varclient varclient
 * @brief Variable Client Functions
 * @{
 */

/*============================================================================*/
/*!
@file varclient.c

    Variable Client Functions

    The Variable Client Functions provide low-level internal client functions
    used by the varserver library

*/
/*============================================================================*/


/*==============================================================================
        Includes
==============================================================================*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/syslog.h>
#include <sys/stat.h>
#include <sys/signalfd.h>
#include <fcntl.h>
#include <mqueue.h>
#include <errno.h>
#include <semaphore.h>
#include <time.h>
#include <string.h>
#include <inttypes.h>
#include <pwd.h>
#include <grp.h>
#include <varserver/varclient.h>

/*==============================================================================
        Private function declarations
==============================================================================*/


/*==============================================================================
        File scoped variables
==============================================================================*/

/*==============================================================================
        Function definitions
==============================================================================*/

/*============================================================================*/
/*  ValidateHandle                                                            */
/*!
    Validate a variable server handle

    The ValidateHandle function checks that the specified handle is a
    handle to the variable server and converts it into VarClient
    pointer.

    @param[in]
        hVarServer
            handle to the Variable Server

    @retval pointer to a VarClient object
    @retval NULL - an invalid variable server handle was specified

==============================================================================*/
VarClient *ValidateHandle( VARSERVER_HANDLE hVarServer )
{
    VarClient *pVarClient;

    pVarClient = (VarClient *)hVarServer;
    if( pVarClient != NULL )
    {
        if( ( pVarClient->id != VARSERVER_ID ) &&
            ( pVarClient->version != VARSERVER_VERSION ) )
        {
            if( pVarClient->debug >= LOG_ERR )
            {
                printf("CLIENT: Invalid VARSERVER handle\n");
            }
            pVarClient = NULL;
        }
    }

    return pVarClient;
}

/*============================================================================*/
/*  ClientRequest                                                             */
/*!
    Send a request from the client to the server

    The ClientRequest function is used to send a client request from a
    client to the Variable Server.

    This is a blocking call.  The client will wait until explicitly
    released by the server.  If the server dies, the client will hang!

    @param[in]
        pVarClient
            pointer to the VarClient object belonging to the client

    @param[in]
        signal
            specifies the readl-time signal to be sent
            from the client to the server

    @retval EOK - the client request was handled successfully by the server
    @retval EINVAL - an invalid client was specified
    @retval other - error code returned by sigqueue, or sem_wait

==============================================================================*/
int ClientRequest( VarClient *pVarClient, int signal )
{
    int result = EINVAL;
    union sigval val;
    ServerInfo *pServerInfo = NULL;

    if( pVarClient != NULL )
    {
         /* get a pointer to the server info */
        pServerInfo = pVarClient->pServerInfo;
        if( pServerInfo != NULL )
        {
            /* provide the client identifier to the var server */
            val.sival_int = pVarClient->clientid;

            if( pVarClient->debug >= LOG_DEBUG )
            {
                printf("CLIENT: Sending client request signal (%d) to %d\n",
                       signal,
                       pServerInfo->pid );
            }

            /* copy the client grouplist into the VarInfo credentials */
            memcpy( pVarClient->variableInfo.creds,
                    pVarClient->grouplist,
                    pVarClient->ngroups * sizeof( gid_t ) );

            /*! set the number of groups to check */
            pVarClient->variableInfo.ncreds = pVarClient->ngroups;

            result = sigqueue( pServerInfo->pid, signal, val );
            if( result == EOK )
            {
                do
                {
                    result = clock_gettime(CLOCK_REALTIME, &pVarClient->ts);
                    if( result == -1)
                    {
                        continue;
                    }

                    // Add 5 second timeout
                    pVarClient->ts.tv_sec += 5;

                    pVarClient->blocked = 1;
                    result = sem_timedwait( &pVarClient->sem, &pVarClient->ts );
                    if( result == EOK )
                    {
                        if( pVarClient->debug >= LOG_DEBUG )
                        {
                            printf("CLIENT: Received response\n");
                        }
                    }
                    else
                    {
                        printf("sem_wait failed\n");
                        result = errno;
                    }
                    pVarClient->blocked = 0;
                }
                while ( result != EOK && result != ETIMEDOUT );
            }
            else
            {
                result = errno;
            }
        }
    }

    if( ( result != EOK ) &&
        ( pVarClient->debug >= LOG_ERR ) )
    {
        printf("%s failed: (%d) %s\n", __func__, result, strerror(result));
    }

    return result;
}

/*! @}
 * end of varclient group */
