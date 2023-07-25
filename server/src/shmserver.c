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
 * @defgroup shmserver shmserver
 * @brief VarServer Shared Memory Interface
 * @{
 */

/*============================================================================*/
/*!
@file shmserver.c

    Variable Server Shared Memory Interface

    The Variable Server shared memory interface contains the server-side
    shared memory communication mechanisms.

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
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syslog.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include <varserver/varclient.h>
#include <varserver/varserver.h>
#include "varlist.h"
#include "taglist.h"
#include "blocklist.h"
#include "transaction.h"
#include "stats.h"
#include "metric.h"
#include "handlers.h"
#include "server.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*! Maximum number of clients */
#define MAX_VAR_CLIENTS                 ( 4096 )

/*! Timer Signal */
#define SIG_TIMER                       ( SIGRTMIN + 5 )

/*==============================================================================
        Private types
==============================================================================*/

/*==============================================================================
        Private function declarations
==============================================================================*/

static int ProcessRequest( siginfo_t *pInfo );
static int GetClientID( void );
static ServerInfo *InitServerInfo( void );

static int NewClient( pid_t pid );

static void RegisterHandler(void(*f)(int sig, siginfo_t *info, void *ucontext));
static void handler(int sig, siginfo_t *info, void *ucontext);

static int CloseClient( VarClient *pVarClient );

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! variable clients */
static VarClient *VarClients[MAX_VAR_CLIENTS+1] = {0};
static ServerInfo *pServerInfo = NULL;

/*==============================================================================
        Public function definitions
==============================================================================*/

/*============================================================================*/
/*  SHMSERVER_Init                                                            */
/*!
    Initialize the Shared Memory Var Server interface

    The SHMSERVER_Init function initializes the shared memory variable server
    interface by creating a shared memory segment to share with the clients
    to report server information, and sets up a real-time signal handler
    to receive signals from clients.

==============================================================================*/
int SHMSERVER_Init( void )
{
    /* Set up server information structure */
    pServerInfo = InitServerInfo();
    if( pServerInfo != NULL )
    {
        /* register the real-time signal handler */
        RegisterHandler(handler);
    }
}

/*============================================================================*/
/*  RegisterHandler                                                           */
/*!
    Register handler function with the real time signals

    The RegisterHandler function registers the specified handler function
    with the real time signals.  The handler function must follow the
    form:

    handler( int sig, siginfo_t *info, void *ucontext )

    @param[in]
        f
            handler function to register with the real time signals

==============================================================================*/
void RegisterHandler(void(*f)(int sig, siginfo_t *info, void *ucontext))
{
    struct sigaction siga;

    siga.sa_sigaction = f;
    siga.sa_flags = SA_SIGINFO;

    /* register the handler function with the real-time signals */
    for (int sig = 1; sig <= SIGRTMAX; ++sig)
    {
        /* register handler function with the signal */
        sigaction(sig, &siga, NULL);
    }
}

/*============================================================================*/
/*  handler                                                                   */
/*!
    Real time signal handler

    The handler function is called to handle any received real-time signals.

    Handled real-time signals include:

    - SIG_NEWCLIENT
    - SIG_CLIENT_REQUEST
    - SIG_TIMER

    @param[in]
        sig
            signal identifier

    @param[in]
        info
            pointer to the siginfo_t object containing the signal info

    @param[in]
        ucontext
            opaque context pointer, unused

==============================================================================*/
void handler(int sig, siginfo_t *info, void *ucontext)
{
    if ( sig == SIG_NEWCLIENT )
    {
        if ( info != NULL )
        {
            NewClient( info->si_pid );
        }
    }
    else if ( sig == SIG_CLIENT_REQUEST )
    {
        ProcessRequest( info );
    }
    else if ( sig == SIG_TIMER )
    {
        STATS_Process();
    }
    else
    {
        printf("SERVER: unhandled signal: %d\n", sig);
    }
}

/*============================================================================*/
/*  InitServerInfo                                                            */
/*!
    Construct the server information which is shared with clients
    via the /varserver shared memory object.

    This information includes:
        - the variable server process identifier used by clients
          to send messages to the server

    @retval pointer to the server information object
    @retval NULL if the server information object could not be created

==============================================================================*/
static ServerInfo *InitServerInfo( void )
{
    int fd;
    int res;
    ServerInfo *pServerInfo = NULL;

    /* get shared memory file descriptor (NOT a file) */
	fd = shm_open(SERVER_SHAREDMEM, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (fd != -1)
	{
    	/* extend shared memory object as it is initialized with size 0 */
	    res = ftruncate(fd, sizeof(ServerInfo));
	    if (res != -1)
	    {
            /* map shared memory to process address space */
            pServerInfo = mmap( NULL,
                                sizeof(ServerInfo),
                                PROT_WRITE,
                                MAP_SHARED,
                                fd,
                                0);

            if( pServerInfo != NULL )
            {
                pServerInfo->pid = getpid();
            }
            else
            {
                perror("mmap");
            }
        }
        else
        {
            perror("ftruncate");
        }
	}
    else
    {
        perror("shm_open");
    }

    return pServerInfo;
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

    for(i=1;i<MAX_VAR_CLIENTS;i++)
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
/*  ProcessRequest                                                            */
/*!

    Process a request from a client

    The ProcessRequest function handles requests received from clients
    Requests include:
        - closing the interface between the client and the server

    @param[in]
        pInfo
            pointer to a siginfo_t object containing the request information

    @retval EOK the request was processed successfully
    @retval EINPROGRESS the request is pending and the client will remain
            blocked until the request is complete
    @retval EINVAL invalid argument

==============================================================================*/
static int ProcessRequest( siginfo_t *pInfo )
{
    int result = EINVAL;
    int clientid;
    VarClient *pVarClient;

    if ( pInfo != NULL )
    {
        /* get the client id */
        clientid = pInfo->si_value.sival_int;

        if( clientid < MAX_VAR_CLIENTS )
        {
            /* get a pointer to the client information object */
            pVarClient = VarClients[clientid];
            result = HandleRequest( pVarClient );
        }
    }

    return result;
}

/*============================================================================*/
/*  NewClient                                                                 */
/*!
    Registers a new client

    The NewClient function registers a new client with the server using
    the client's process identifier.  Once a client has been created
    and registered, it may send requests to the server using the
    variable server APIs.

    @param[in]
        pid
            The new client's process identifier

    @retval EOK the client was created and registered successfully
    @retval EINVAL the new client could not be created

==============================================================================*/
static int NewClient( pid_t pid )
{
    int fd;
    char clientname[BUFSIZ];
    VarClient *pVarClient;
    int result = EINVAL;
    int clientId;

    sprintf(clientname, "/varclient_%d", pid);

    /* get shared memory file descriptor (NOT a file) */
	fd = shm_open( clientname, O_RDWR, S_IRUSR | S_IWUSR);
	if (fd != -1)
	{
        /* map shared memory to process address space */
        pVarClient = (VarClient *)mmap( NULL,
                                        sizeof(VarClient),
                                        PROT_WRITE,
                                        MAP_SHARED,
                                        fd,
                                        0);
        if (pVarClient != MAP_FAILED)
        {
            clientId=GetClientID();
            if ( clientId != 0 )
            {
                VarClients[clientId] = pVarClient;
                pVarClient->rr.clientid = clientId;
                pVarClient->pFnClose = CloseClient;
            }
            else
            {
                pVarClient->rr.clientid = 0;
            }

            /* close the file descriptor since we don't need it for anything */
            close( fd );

            UnblockClient( pVarClient );

            if ( clientId == 0 )
            {
                munmap( pVarClient, sizeof(VarClient));
            }

            result = EOK;
        }
        else
        {
            perror("mmap");
        }
	}
    else
    {
        perror("shm_open");
    }

    return result;
}

/*============================================================================*/
/*  PrintClientInfo                                                           */
/*!
    Print Client Runtime information

    The PrintClientInfo function iterates through all the clients
    and prints the client's runtime statistics into the output buffer.

    @param[in]
        pVarInfo
            pointer to a VarInfo object for the variable associated with the
            client info

    @param[in,out]
        buf
            pointer to the output buffer to print the information into

    @param[in]
        len
            length of the output buffer to print the information into

    @retval EOK output generated successfully
    @retval EINVAL invalid arguments

==============================================================================*/
int PrintClientInfo( VarInfo *pVarInfo, char *buf, size_t len )
{
    size_t i;
    size_t offset = 0;
    size_t n;
    VarClient *pVarClient;
    int result = EINVAL;

    if ( ( pVarInfo != NULL ) &&
         ( buf != NULL ) )
    {
        /* get the variable TLV */
        pVarInfo->var.type = VARTYPE_STR;
        pVarInfo->var.len = len;
        strcpy(pVarInfo->formatspec, "%s");

        buf[0] = '\n';
        offset = 1;

        for(i=1;i<MAX_VAR_CLIENTS;i++)
        {
            pVarClient = VarClients[i];
            if( pVarClient != NULL )
            {
                n = snprintf( &buf[offset],
                            len,
                            "id: %d, blk: %d, txn: %lu, pid: %d\n",
                            pVarClient->rr.clientid,
                            pVarClient->blocked,
                            pVarClient->transactionCount,
                            pVarClient->rr.client_pid );

                offset += n;
                len -= n;
            }
        }

        buf[offset]=0;

        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  CloseClient                                                               */
/*!
    Close the Variable Client

    The CloseClient function closes the Variable Client and
    unmaps its shared memory segment.

    @param[in]
        pVarClient
            pointer to the VarClient to close

==============================================================================*/
static int CloseClient( VarClient *pVarClient )
{
    int clientid;
    int result = EINVAL;

    if ( pVarClient != NULL )
    {
        /* get the client id */
        clientid = pVarClient->rr.clientid;

        if( ( clientid > 0 ) && ( clientid < MAX_VAR_CLIENTS ) )
        {
            /* clear the client entry in the VAR clients table */
            VarClients[clientid] = NULL;
        }

        /* unmap the memory */
        result = munmap( pVarClient, sizeof(VarClient));
        if( result != EOK )
        {
            result = errno;
        }

        if( ( result != EOK ) &&
            ( pVarClient->debug >= LOG_DEBUG ) )
        {
            printf("%s failed: (%d) %s\n", __func__, result, strerror(result));
        }
    }

    return result;
}

/*! @}
 * end of shmserver group */
