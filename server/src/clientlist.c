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

/*==============================================================================
        Private types
==============================================================================*/


/*==============================================================================
        Private function declarations
==============================================================================*/

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! active client list */
static VarClient *clientlist = NULL;

/*! list of available VarClient objects */
static VarClient *freelist = NULL;

/*! number of active and inactive clients */
static int NumClients = 0;

/*! number of active clients */
static int ActiveClients = 0;

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

            /* allocate new client identifier */
            pVarClient->rr.clientid = ++NumClients;
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

        /* increment the number of active clients */
        ActiveClients++;

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

        /* put the VarClient on the head of the free list */
        pVarClient->pNext = freelist;
        pVarClient->active = false;
        freelist = pVarClient;

        /* decrement the number of active clients */
        ActiveClients--;
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

/*! @}
 * end of clientlist group */
