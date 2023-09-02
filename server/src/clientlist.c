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
 * @defgroup clientlist clientlist
 * @brief Manages a list of clients
 * @{
 */

/*============================================================================*/
/*!
@file clientlist.c

    ClientList

    The Client List manages a list of clients.

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

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
#include <varserver/varclient.h>
#include "clientlist.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*! Maximum number of clients */
#define MAX_VAR_CLIENTS                 ( 4096 )

/*==============================================================================
        Private types
==============================================================================*/


/*==============================================================================
        Private function declarations
==============================================================================*/

static int GetClientID( void );

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! active client list */
static VarClient *clientlist = NULL;

/*! list of available VarClient objects */
static VarClient *freelist = NULL;

/*! number of active clients */
static int ActiveClients = 0;

static VarClient *VarClients[MAX_VAR_CLIENTS+1] = {0};

/*==============================================================================
        Public function definitions
==============================================================================*/

/*============================================================================*/
/*  NewClient                                                                 */
/*!
    Create a new VarClient

    The NewClient function creates a new VarClient object

    @param[in]
        sd
            client socket descriptor

    @param[in]
        workbufsize
            size of the client working buffer

    @retval pointer to the new client
    @retval NULL if the new client could not be created

==============================================================================*/
VarClient *NewClient( int sd, size_t workbufsize )
{
    VarClient *pVarClient = NULL;
    VarClient *p;
    VarClient *pLast;

    /* search the free list for a client with the correct workbuf size */
    if ( freelist != NULL )
    {
        pLast = NULL;
        p = freelist;

        /* search the free list for a client with the correct workbuf length */
        while( p != NULL )
        {
            if ( p->workbufsize == workbufsize )
            {
                /* found one */
                break;
            }

            pLast = p;
            p = p->pNext;
        }

        /* check if we have found a client with the correct workbuf length */
        if ( p != NULL )
        {
            if ( p == freelist )
            {
                freelist = p->pNext;
            }

            if ( pLast != NULL )
            {
                pLast->pNext = p->pNext;
            }

            pVarClient = p;
            pVarClient->pNext = NULL;
        }
        else
        {
            p = freelist;
            freelist = freelist->pNext;

            /* reallocate client workbuf size */
            pVarClient = realloc( p, sizeof( VarClient ) + workbufsize );
            if ( pVarClient != NULL )
            {
                pVarClient->workbufsize = workbufsize;
            }
        }
    }
    else
    {
        /* allocate a new VarClient */
        pVarClient = calloc( 1, sizeof( VarClient ) + workbufsize );
        if ( pVarClient != NULL )
        {
            /* set the workbuf size */
            pVarClient->workbufsize = workbufsize;
        }
    }

    if ( pVarClient != NULL )
    {
        /* set the client active flag */
        pVarClient->active = true;

        /* initialize the payload length */
        pVarClient->rr.len = 0;

        /* initialize the socket descriptor */
        pVarClient->sd = sd;

        /* allocate new client identifier */
        SetNewClient( pVarClient );

        /* add the VarClient to the client list */
        pVarClient->pNext = clientlist;
        clientlist = pVarClient;
    }

    return pVarClient;
}

/*============================================================================*/
/*  DeleteClient                                                              */
/*!
    Delete a VarClient

    The DeleteClient function deletes a VarClient object
    from the active client list and places it on the free client list

    @param[in]
        pVarClient
            pointer to the client to delete

==============================================================================*/
void DeleteClient( VarClient *pVarClient )
{
    VarClient *p;
    VarClient *pLast;

    if ( pVarClient != NULL )
    {
        if ( clientlist == pVarClient )
        {
            /* remove from the head of the list */
            clientlist = pVarClient->pNext;
        }
        else
        {
            /* scan client list */
            pLast = clientlist;
            p = clientlist->pNext;

            while ( p != NULL )
            {
                if ( p == pVarClient )
                {
                    /* remove client */
                    pLast->pNext = pVarClient->pNext;
                }

                pLast = p;
                p = p->pNext;
            }
        }

        /* mark the client as inactive */
        ClearClient(pVarClient);

        /* put the VarClient on the head of the free list */
        pVarClient->pNext = freelist;
        pVarClient->active = false;
        freelist = pVarClient;
    }
}

/*============================================================================*/
/*  GetActiveClients                                                          */
/*!
    Get Active Clients

    The GetClientClients function retrieves the number of active clients

    @retval number of active clients

==============================================================================*/
int GetActiveClients(void)
{
    return ActiveClients;
}

/*============================================================================*/
/*  FindClient                                                                */
/*!
    Search the client list for the specified client

    The FindClient function searches the client list looking for the
    client with the specified client id

    @retval pointer to the found client
    @retval NULL if the client was not found

==============================================================================*/
VarClient *FindClient( int client_id )
{
    VarClient *pVarClient = clientlist;

    while ( pVarClient != NULL )
    {
        if ( pVarClient->rr.clientid == client_id )
        {
            /* found the matching client */
            break;
        }

        pVarClient = pVarClient->pNext;
    }

    return pVarClient;
}

/*============================================================================*/
/*  GetClientID                                                               */
/*!
    Get an available client identifier

    The GetClientID function searches the variable server client list
    and returns the first available client identifier

    @retval an available client identifier
    @retval 0 if no client identifiers are available

==============================================================================*/
static int GetClientID( void )
{
    int i;
    int clientId = 0;

    for(i=1;i<=MAX_VAR_CLIENTS;i++)
    {
        if( VarClients[i] == NULL )
        {
            clientId=i;
            break;
        }
    }

    return clientId;
}

/*============================================================================*/
/*  GetClientByID                                                             */
/*!
    Get a client object given its client id

    The GetClientByID function gets the VarClient object associated
    with the specified client id. If the client id is invalid or out of
    range, a NULL pointer will be returned.

    @retval pointer to the VarClient object associated with the client id
    @retval  NULL invalid client id

==============================================================================*/
VarClient *GetClientByID( int clientID )
{
    VarClient *pVarClient = NULL;

    if ( ( clientID > 0 ) && ( clientID <= MAX_VAR_CLIENTS ) )
    {
        pVarClient = VarClients[clientID];
    }

    return pVarClient;
}


/*============================================================================*/
/*  GetClientInfo                                                             */
/*!
    Get a client debug information

    The GetClientInfo function prints the VarClient debug information
    into a buffer.

    Information includes;
        - client id
        - client blocked status
        - number of transactions
        - process identifier (for signal clients)
        - socket descriptor (for socket clients)

    @retval pointer to the VarClient object associated with the client id
    @retval  NULL invalid client id

==============================================================================*/
int GetClientInfo( char *buf, size_t len )
{
    VarClient *pVarClient;
    size_t i;
    size_t offset = 0;
    size_t n = 0;

    if ( len > 0 )
    {
        buf[0] = '\n';
        offset = 1;

        for(i=1;i<=MAX_VAR_CLIENTS;i++)
        {
            pVarClient = VarClients[i];
            if( pVarClient != NULL )
            {
                n = snprintf( &buf[offset],
                            len,
                            "id: %d, blk: %d, txn: %lu, pid: %d sd: %d\n",
                            pVarClient->rr.clientid,
                            pVarClient->blocked,
                            pVarClient->transactionCount,
                            pVarClient->rr.client_pid,
                            pVarClient->sd );

                offset += n;
                len -= n;
            }

            if ( len <= 0 )
            {
                break;
            }
        }

        buf[offset]=0;
    }

    return n;
}

/*============================================================================*/
/*  SetNewClient                                                              */
/*!
    Store a new VarClient object into the client list

    The SetNewClient function stores the specified client object
    into the VarClient list and sets its corresponding client identifier.

    @param[in]
        pVarClient
            pointer to the VarClient object to store

    @retval EOK client stored ok
    @retval EINVAL invalid arguments
    @retval ENOMEM max clients reached

==============================================================================*/
int SetNewClient( VarClient *pVarClient )
{
    int result = EINVAL;
    int clientID;

    if ( pVarClient != NULL )
    {
        clientID = GetClientID();
        if ( clientID > 0 )
        {
            VarClients[clientID] = pVarClient;
            pVarClient->rr.clientid = clientID;
            ActiveClients++;
            result = EOK;
        }
        else
        {
            result = ENOMEM;
        }
    }

    return result;
}

/*============================================================================*/
/*  ClearClient                                                               */
/*!
    Remove a client reference from the client list

    The ClearClient function removes the client object reference
    from the client list.

    @param[in]
        pVarClient
            pointer to the VarClient object to remove

    @retval EOK client removed ok
    @retval EINVAL invalid arguments
    @retval ENOTSUP client reference and client id do not match
    @retval ENOENT invalid client ID

==============================================================================*/
int ClearClient( VarClient *pVarClient )
{
    int result = EINVAL;
    int clientID;

    if ( pVarClient != NULL )
    {
        clientID = pVarClient->rr.clientid;
        if ( ( clientID > 0 ) && ( clientID <= MAX_VAR_CLIENTS ) )
        {
            if ( VarClients[clientID] == pVarClient )
            {
                VarClients[clientID] = NULL;
                pVarClient->rr.clientid = 0;
                ActiveClients--;
                result = EOK;
            }
            else
            {
                result = ENOTSUP;
            }
        }
        else
        {
            result = ENOENT;
        }
    }

    return result;
}

/*! @}
 * end of clientlist group */
