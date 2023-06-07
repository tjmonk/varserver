/*============================================================================
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
============================================================================*/

/*!
 * @defgroup blocklist blocklist
 * @brief Manages a list of clients which are blocked on a transaction
 * @{
 */

/*==========================================================================*/
/*!
@file blocklist.c

    BlockList

    The Block List manages a list of clients which are currently
    blocked waiting for a transaction to complete.

*/
/*==========================================================================*/

/*============================================================================
        Includes
============================================================================*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include "varclient.h"
#include "blocklist.h"

/*============================================================================
        Private definitions
============================================================================*/


/*============================================================================
        Private types
============================================================================*/

/*! the RequestHandler object defines a request handler */
typedef struct _BlockedClient
{
    /*! the type of request this handler is for */
    NotificationType notifyType;

    /*! pointer to the variable client which is blocked */
    VarClient *pVarClient;

    /*! pointer to the next blocked client */
    struct _BlockedClient *pNext;

} BlockedClient;

/*============================================================================
        Private function declarations
============================================================================*/

/*============================================================================
        Private file scoped variables
============================================================================*/

/*! blocked clients */
static BlockedClient *blockedClients = NULL;

/*! list of available BlockedClient objects */
static BlockedClient *freelist = NULL;

/*============================================================================
        Public function definitions
============================================================================*/

/*==========================================================================*/
/*  BlockClient                                                             */
/*!
    Block a client while it waits for a transaction to complete

    The BlockClient function adds the specified client to the
    blocked client list

    @param[in]
        pVarClient
            pointer to the client to block

    @param[in]
        notifyType
            indicates the type of notification the client is blocked on

    @retval EOK the client was successfully added to the blocked client list
    @retval EINVAL invalid arguments
    @retval ENOMEM memory allocation problem

============================================================================*/
int BlockClient( VarClient *pVarClient, NotificationType notifyType )
{
    int result = EINVAL;
    BlockedClient *pBlockedClient;

    if( pVarClient != NULL )
    {
        if( freelist != NULL )
        {
            /* get a new blocked client object from the free list */
            pBlockedClient = freelist;
            freelist = freelist->pNext;
        }
        else
        {
            /* allocate a new blocked client object */
            pBlockedClient = calloc( 1, sizeof( BlockedClient ) );
        }

        if( pBlockedClient != NULL )
        {
            /* populate the blocked client object */
            pBlockedClient->notifyType = notifyType;
            pBlockedClient->pVarClient = pVarClient;

            /* insert the blocked client on the head of the blocked
               client list */
            pBlockedClient->pNext = blockedClients;
            blockedClients = pBlockedClient;

            result = EOK;
        }
        else
        {
            result = ENOMEM;
        }
    }

    return result;
}

/*==========================================================================*/
/*  UnblockClients                                                          */
/*!
    Unblock clients which are waiting on the specified variable

    The UnblockClients function iterates through the blocked clients
    list looking for any clients which are blocked on the specified
    variable waiting for the specified notification.

    Any matching blocked clients are unblocked

    @param[in]
        hVar
            variable the client is blocked against

    @param[in]
        notifyType
            indicates the type of notification the client is blocked on

    @retval EOK one or more clients was unblocked
    @retval ENOENT no blocked clients were found

============================================================================*/
int UnblockClients( VAR_HANDLE hVar,
                    NotificationType notifyType,
                    int (*cb)( VarClient *pVarClient, void *arg ),
                    void *arg )
{
    int result = ENOENT;

    BlockedClient *pBlockedClient = blockedClients;
    BlockedClient *pPrevClient = blockedClients;
    VarClient *pVarClient;

    while( pBlockedClient != NULL )
    {
        if( pBlockedClient->pVarClient != NULL )
        {
            /* get a pointer to the blocked varclient */
            pVarClient = pBlockedClient->pVarClient;
            if( ( pBlockedClient->notifyType == notifyType ) &&
                ( pVarClient->variableInfo.hVar == hVar ) )
            {
                /* found a match */
                if( pVarClient->debug >= LOG_DEBUG )
                {
                    printf( "SERVER: unblocking client %d pid(%d)\n",
                            pVarClient->clientid,
                            pVarClient->client_pid );
                }

                if( cb != NULL )
                {
                    cb( pVarClient, arg );
                }

                /* unblock the client by posting to the client semaphore */
                sem_post( &pVarClient->sem );

                /* remove the blocked client from the blocked client list */
                if( pPrevClient == blockedClients )
                {
                    /* remove the blocked client from
                       the head of the blocked client list */
                    blockedClients = pBlockedClient->pNext;
                }
                else
                {
                    /* remove the blocked client from the interior
                       of the blocked client list */
                    pPrevClient->pNext = pBlockedClient->pNext;
                }

                /* move the blocked client to the free list */
                pBlockedClient->notifyType = NOTIFY_NONE;
                pBlockedClient->pVarClient = NULL;
                pBlockedClient->pNext = freelist;
                freelist = pBlockedClient;

                /* indicate that a client was unblocked */
                result = EOK;
            }
            else
            {
                /* update the pointer to the previous client
                   which is still in the blocked client list */
                pPrevClient = pBlockedClient;
            }
        }

        /* move on to the next blocked client */
        pBlockedClient = pBlockedClient->pNext;
    }
}

/*! @}
 * end of blocklist group */
