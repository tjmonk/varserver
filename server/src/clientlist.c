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
#define MAX_VAR_CLIENTS                 ( 256 )

/*==============================================================================
        Private types
==============================================================================*/


/*==============================================================================
        Private function declarations
==============================================================================*/

static void UpdateClientSDMap( void );

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! active client list */
static VarClient *clientlist = NULL;

/*! list of available VarClient objects */
static VarClient *freelist = NULL;

/*! ClientID to client mapping (index 0 not used)*/
static VarClient *clientmap[MAX_VAR_CLIENTS] = {0};

/*! socket descriptor map */
static int sdmap[MAX_VAR_CLIENTS] = {-1};

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
    end of the blocked client list

    @param[in]
        sd
            client socket descriptor

    @retval pointer to the new client
    @retval NULL if the new client could not be created

==============================================================================*/
VarClient *NewClient( int sd )
{
    VarClient *pVarClient = NULL;

    if ( freelist != NULL )
    {
        /* get the VarClient from the free list */
        pVarClient = freelist;
        freelist = pVarClient->pNext;
    }
    else
    {
        /* allocate a new VarClient */
        pVarClient = calloc( 1, sizeof( VarClient ) );
        if ( pVarClient != NULL )
        {
            /* allocate new client identifier */
            pVarClient->rr.clientid = ++NumClients;
        }
    }

    if ( pVarClient != NULL )
    {
        /* set the client active flag */
        pVarClient->active = true;

        /* set the client socket descriptor */
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
/*  GetClient                                                                 */
/*!
    Get a VarClient

    The GetClient function retrieves an active client given
    its index in the client map.  If the client does not exist
    or is inactive then no client pointer is returned.

    @param[in]
        idx
            index into the client map

    @retval pointer to VarClient associated with clientid
    @retval NULL invalid client id

==============================================================================*/
VarClient *GetClient( int idx )
{
    VarClient *pVarClient = NULL;

    if ( idx < NumClients )
    {
        /* get the VarClient from the client map */
        pVarClient = clientmap[idx];
        if ( pVarClient->active == false )
        {
            /* we do not return inactive client objects */
            pVarClient = NULL;
        }
    }

    return pVarClient;
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
/*  GetClientfds                                                              */
/*!
    Get clients fd_set

    The GetClientfds function updates the specified fd_set with
    the socket descriptors of each of the active clients.
    While doing so, it also calculates the maximum socket
    descriptor processed, which is returned in the result

    @param[in]
        max_sd
            the maximum socket descriptor in the specified fd_set

    @param[in,out]
        pfds
            pointer to an fd_set of socket descriptors to be updated

    @retval maximum socket descriptor in the updated fd_set

==============================================================================*/
int GetClientfds( int max_sd, fd_set *pfds )
{
    int i = 0;
    int sd;

    /* update the client socket descriptor map */
    UpdateClientSDMap();

    /* convert the client socket descriptor map to an fd_set */
    if ( pfds != NULL )
    {
        do
        {
            /* socket descriptor */
            sd = sdmap[i++];

            /* if valid socket descriptor then add to read list */
            if ( sd > 0 )
            {
                FD_SET( sd, pfds );

                /* track highest socket descriptor number */
                if ( sd > max_sd )
                {
                    max_sd = sd;
                }
            }
        }
        while ( sd != 0 );
    }

    return max_sd;
}

/*============================================================================*/
/*  GetClientSDMap                                                            */
/*!
    Get Client Socket Descriptor Map

    The GetClientSocketDescriptorMap function gets an array
    of socket descriptors for the active clients

    @retval pointer to the array of socket descriptors

==============================================================================*/
int *GetClientSDMap( void )
{
    return &sdmap[0];
}

/*============================================================================*/
/*  UpdateClientSDMap                                                         */
/*!
    Update the Client Socket Descriptor Map

    The UpdateClientSDMap function generates an array of active socket
    descriptors to be monitored

==============================================================================*/
static void UpdateClientSDMap( void )
{
    int i = 0;
    VarClient *pVarClient = clientlist;

    while ( pVarClient != NULL )
    {
        sdmap[i] = pVarClient->sd;
        clientmap[i] = pVarClient;
        pVarClient = pVarClient->pNext;
        i++;
    }

    /* NUL terminate the socket descriptor and client maps */
    sdmap[i] = 0;
    clientmap[i] = NULL;
}

/*! @}
 * end of clientlist group */
