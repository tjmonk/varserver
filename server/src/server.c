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

/*! the RequestHandler object defines a request handler */
typedef struct _RequestHandler
{
    /*! the type of request this handler is for */
    VarRequest requestType;

    /*! request name */
    char *requestName;

    /*! pointer to the request handler function */
    int (*handler)( VarClient *pVarClient );

    /*! pointer to the name of the metric counter */
    char *pMetricName;

    /*! counter for the number of times this request has been made */
    uint64_t *pMetric;

} RequestHandler;

/*==============================================================================
        Private function declarations
==============================================================================*/
static int NewClient( pid_t pid );
static int ProcessRequest( siginfo_t *pInfo );
static int UnblockClient( VarClient *pVarClient );
static int GetClientID( void );
static int SetupServerInfo( void );
static ServerInfo *InitServerInfo( void );
static int ValidateClient( VarClient *pVarClient );

static int ProcessVarRequestInvalid( VarClient *pVarClient );
static int ProcessVarRequestClose( VarClient *pVarClient );
static int ProcessVarRequestEcho( VarClient *pVarClient );
static int ProcessVarRequestNew( VarClient *pVarClient );
static int ProcessVarRequestFind( VarClient *pVarClient );
static int ProcessVarRequestPrint( VarClient *pVarClient );
static int ProcessVarRequestSet( VarClient *pVarClient );
static int ProcessVarRequestType( VarClient *pVarClient );
static int ProcessVarRequestName( VarClient *pVarClient );
static int ProcessVarRequestLength( VarClient *pVarClient );
static int ProcessVarRequestGet( VarClient *pVarClient );
static int ProcessVarRequestNotify( VarClient *pVarClient );
static int ProcessValidationRequest( VarClient *pVarClient );
static int ProcessValidationResponse( VarClient *pVarClient );
static int ProcessVarRequestOpenPrintSession( VarClient *pVarClient );
static int ProcessVarRequestClosePrintSession( VarClient *pVarClient );
static int ProcessVarRequestGetFirst( VarClient *pVarClient );
static int ProcessVarRequestGetNext( VarClient *pVarClient );

static uint64_t *MakeMetric( char *name );

static int InitStats( void );

static int PrintClientInfo( VarInfo *pVarInfo, char *buf, size_t len );

void RegisterHandler(void(*f)(int sig, siginfo_t *info, void *ucontext));
void handler(int sig, siginfo_t *info, void *ucontext);

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! variable clients */
static VarClient *VarClients[MAX_VAR_CLIENTS+1] = {0};
static ServerInfo *pServerInfo = NULL;
static VAR_HANDLE hClientInfo = VAR_INVALID;

/*! Request Handlers - these must appear in the exact same order
    as the request enumerations so they can be looked up directly
    in the Request array */
static RequestHandler RequestHandlers[] =
{
    {
        VARREQUEST_INVALID,
        "IMVALID",
        ProcessVarRequestInvalid,
        NULL,
        NULL
    },
    {
        VARREQUEST_OPEN,
        "OPEN",
        NULL,
        NULL,
        NULL
    },
    {
        VARREQUEST_CLOSE,
        "CLOSE",
        ProcessVarRequestClose,
        "/varserver/stats/close",
        NULL
    },
    {
        VARREQUEST_ECHO,
        "ECHO",
        ProcessVarRequestEcho,
        "/varserver/stats/echo",
        NULL
    },
    {
        VARREQUEST_NEW,
        "NEW",
        ProcessVarRequestNew,
        "/varserver/stats/new",
        NULL
    },
    {
        VARREQUEST_FIND,
        "FIND",
        ProcessVarRequestFind,
        "/varserver/stats/find",
        NULL
    },
    {
        VARREQUEST_GET,
        "GET",
        ProcessVarRequestGet,
        "/varserver/stats/get",
        NULL
    },
    {
        VARREQUEST_PRINT,
        "PRINT",
        ProcessVarRequestPrint,
        "/varserver/stats/print",
        NULL
    },
    {
        VARREQUEST_SET,
        "SET",
        ProcessVarRequestSet,
        "/varserver/stats/set",
        NULL
    },
    {
        VARREQUEST_TYPE,
        "TYPE",
        ProcessVarRequestType,
        "/varserver/stats/type",
        NULL
    },
    {
        VARREQUEST_NAME,
        "NAME",
        ProcessVarRequestName,
        "/varserver/stats/name",
        NULL
    },
    {
        VARREQUEST_LENGTH,
        "LENGTH",
        ProcessVarRequestLength,
        "/varserver/stats/length",
        NULL
    },
    {
        VARREQUEST_NOTIFY,
        "NOTIFY",
        ProcessVarRequestNotify,
        "/varserver/stats/notify",
        NULL
    },
    {
        VARREQUEST_GET_VALIDATION_REQUEST,
        "VALIDATION_REQUEST",
        ProcessValidationRequest,
        "/varserver/stats/validate_request",
        NULL
    },
    {
        VARREQUEST_SEND_VALIDATION_RESPONSE,
        "VALIDATION_RESPONSE",
        ProcessValidationResponse,
        "/varserver/stats/validation_response",
        NULL
    },
    {
        VARREQUEST_OPEN_PRINT_SESSION,
        "OPEN_PRINT_SESSION",
        ProcessVarRequestOpenPrintSession,
        "/varserver/stats/open_print_session",
        NULL
    },
    {
        VARREQUEST_CLOSE_PRINT_SESSION,
        "CLOSE_PRINT_SESSION",
        ProcessVarRequestClosePrintSession,
        "/varserver/stats/close_print_session",
        NULL
    },
    {
        VARREQUEST_GET_FIRST,
        "GET_FIRST",
        ProcessVarRequestGetFirst,
        "/varserver/stats/get_first",
        NULL
    },
    {
        VARREQUEST_GET_NEXT,
        "GET_FIRST",
        ProcessVarRequestGetNext,
        "/varserver/stats/get_next",
        NULL
    }
};

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
    sigset_t mask;
    siginfo_t info;
    int signum;
    int count = 0;
    ServerInfo *pServerInfo = NULL;

    /* Set up server information structure */
    pServerInfo = InitServerInfo();
    if( pServerInfo != NULL )
    {
        /* initialize the varserver statistics */
        InitStats();

        /* register the real-time signal handler */
        RegisterHandler(handler);

        /* loop forever processing signals */
        while(1)
        {
            /* do nothing - handler functions take care of everything */
            pause();
        }
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
    VarRequest requestType;
    int (*handler)(VarClient *pVarClient);
    uint64_t *pMetric;

    if ( pInfo != NULL )
    {
        /* get the client id */
        clientid = pInfo->si_value.sival_int;

        /* update the request stats */
        STATS_IncrementRequestCount();

        if( clientid < MAX_VAR_CLIENTS )
        {
            /* get a pointer to the client information object */
            pVarClient = VarClients[clientid];
            if( pVarClient != NULL )
            {
                if( pVarClient->rr.requestType >= VARREQUEST_END_MARKER )
                {
                    requestType = VARREQUEST_INVALID;
                }
                else
                {
                    requestType = pVarClient->rr.requestType;
                }

                if( pVarClient->debug >= LOG_DEBUG )
                {
                    printf("SERVER: Processing request %s from client %d\n",
                            RequestHandlers[requestType].requestName,
                            clientid);
                }

                /* update the metric */
                pMetric = RequestHandlers[requestType].pMetric;
                if ( pMetric != NULL )
                {
                    (*pMetric)++;
                }

                /* increment the client's transaction counter */
                (pVarClient->transactionCount)++;

                /* get the appropriate handler */
                handler = RequestHandlers[requestType].handler;
                if( handler != NULL )
                {
                    /* invoke the handler */
                    result = handler( pVarClient );
                }
                else
                {
                    /* this function is not supported yet */
                    printf("requestType %d is not supported\n", requestType);
                    result = ENOTSUP;
                }
            }
            else
            {
                printf("SERVER: Invalid var client : NULL pointer\n");
            }
        }
        else
        {
            printf("SERVER: Invalid client ID: %d\n", clientid);
        }

        /* a result code of EINPROGRESS indicates the the variable server
           has passed the requested transaction off to another client
           unblocking this client request will be deferred until
           the other client responds */
        if( result != EINPROGRESS )
        {
            /* unblock the client so it can proceed */
            if( requestType != VARREQUEST_CLOSE )
            {
                UnblockClient( pVarClient );
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestClose                                                    */
/*!
    Terminate a client connection

    Handle a request from a client to terminate its connection to the
    server.  The client is unblocked, the client memory map is closed,
    and the client reference is deleted.

    @param[in]
        pVarClient
            pointer to the variable client object

    @retval EOK the client was closed successfully
    @retval EINVAL invalid argument
    @retval errno error number returned by munmap

==============================================================================*/
static int ProcessVarRequestClose( VarClient *pVarClient )
{
    int result = EINVAL;
    int clientid = 0;

    if( pVarClient != NULL )
    {
        if( pVarClient->debug >= LOG_DEBUG )
        {
            printf("SERVER: Closing Client\n");
        }

        pVarClient->rr.responseVal = 0;

        /* allow the client to proceed */
        UnblockClient( pVarClient );

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

/*============================================================================*/
/*  UnblockClient                                                             */
/*!
    Unblock the client connection

    Unblocking the client connection allows the client to return from its
    API call to the server.

    @param[in]
        pVarClient
            pointer to the variable client object

    @retval EOK the client was closed successfully
    @retval EINVAL invalid argument

==============================================================================*/
static int UnblockClient( VarClient *pVarClient )
{
    int result = EINVAL;

    if( pVarClient != NULL )
    {
        if( pVarClient->debug >= LOG_DEBUG )
        {
            printf( "SERVER: unblocking client %d pid(%d)\n",
                    pVarClient->rr.clientid,
                    pVarClient->rr.client_pid );
        }

        /* unblock the client by posting to the client semaphore */
        sem_post( &pVarClient->sem );
        result = EOK;
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
/*  ValidateClient                                                            */
/*!
    Validate a client reference

    The ValidateClient function checks that the specified pointer
    references a valid varclient for this server.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the client is valid
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ValidateClient( VarClient *pVarClient )
{
    int result = EINVAL;

    if( ( pVarClient != NULL ) &&
        ( pVarClient->rr.id == VARSERVER_ID ) )
    {
        if( pVarClient->rr.version == VARSERVER_VERSION )
        {
            result = EOK;
        }
        else
        {
            result = ENOTSUP;
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestNew                                                      */
/*!
    Process a NEW variable request from a client

    The ProcessVarRequestNew function handles a "NEW variable" request
    from a client.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the new variable was created
    @retval ENOMEM memory allocation failure
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ProcessVarRequestNew( VarClient *pVarClient )
{
    int result = EINVAL;
    uint32_t varhandle;
    int rc;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* special handling for string variables uses the client's
           working buffer to pass the variable value */
        if( pVarClient->rr.variableInfo.var.type == VARTYPE_STR )
        {
            pVarClient->rr.variableInfo.var.val.str = &pVarClient->workbuf;
        }

        /* special handling for blob variables uses the client's
           working buffer to pass the variable value */
        if( pVarClient->rr.variableInfo.var.type == VARTYPE_BLOB )
        {
            pVarClient->rr.variableInfo.var.val.blob = &pVarClient->workbuf;
        }

        /* add the new variable to the variable list */
        rc = VARLIST_AddNew( &pVarClient->rr.variableInfo, &varhandle );
        if( rc == EOK )
        {
            /* return the new variable's handle */
            pVarClient->rr.responseVal = varhandle;
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestFind                                                     */
/*!
    Process a FIND variable request from a client

    The ProcessVarRequestFind function handles a "FIND variable" request
    from a client.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the new variable was found
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ProcessVarRequestFind( VarClient *pVarClient )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* search for the variable in the variable list */
        result = VARLIST_Find( &pVarClient->rr.variableInfo,
                               (VAR_HANDLE *)&pVarClient->rr.responseVal );
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestEcho                                                     */
/*!
    Process an ECHO request from a client

    The ProcessVarRequestEcho function handles a test ECHO request
    from a client.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the request was successful
    @retval EINVAL the client is invalid

==============================================================================*/
static int ProcessVarRequestEcho( VarClient *pVarClient )
{
    int result = EINVAL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* echo request back to response */
        pVarClient->rr.responseVal = pVarClient->rr.requestVal;
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestInvalid                                                  */
/*!
    Handler for invalid requests from a client

    The ProcessVarRequestInvalid function catches any unsupported/invalid
    request from a client.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval ENOTSUP the request is not supported
    @retval EINVAL the client is invalid

==============================================================================*/
static int ProcessVarRequestInvalid( VarClient *pVarClient )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* tell the client this request is not supported */
        result = ENOTSUP;
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestPrint                                                    */
/*!
    Process a PRINT variable request from a client

    The ProcessVarRequestPrint function handles a "PRINT variable" request
    from a client.  It populates the variable value and the format specifier
    into the client's VarInfo object

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable info was returned
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ProcessVarRequestPrint( VarClient *pVarClient )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;
    pid_t handler;
    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        if ( pVarClient->rr.variableInfo.hVar == hClientInfo )
        {
            result = PrintClientInfo( &pVarClient->rr.variableInfo,
                                      &pVarClient->workbuf,
                                      pVarClient->workbufsize );
        }
        else
        {
            result = VARLIST_PrintByHandle( pVarClient->rr.client_pid,
                                            &pVarClient->rr.variableInfo,
                                            &pVarClient->workbuf,
                                            pVarClient->workbufsize,
                                            pVarClient,
                                            &handler );
        }

        /* capture the result */
        pVarClient->rr.responseVal = result;

        if( result == EINPROGRESS )
        {
            /* another client needs to calculate the value before
               we can print it */
            /* add the client to the blocked clients list */
            BlockClient( pVarClient, NOTIFY_CALC );
        }
        else if(  result == ESTRPIPE )
        {
            /* printing is being handled by another client */
            /* get the PID of the client handling the printing */
            pVarClient->rr.peer_pid = handler;

            /* don't unblock the requesting client */
            result = EINPROGRESS;
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestOpenPrintSession                                         */
/*!
    Handle a request to open a print session

    The ProcessVarRequestOpenPrintSession function handles a request
    from a client to open a print session.  It gets the variable handle
    for the variable being printed, and gets the VarClient object
    for the client which is requesting the variable to be printed.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable info was returned
    @retval ENOENT the print session was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ProcessVarRequestOpenPrintSession( VarClient *pVarClient )
{
    int result = EINVAL;
    VarClient *pRequestor;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* get the print session transaction */
        pRequestor = (VarClient *)TRANSACTION_Get( pVarClient->rr.requestVal );
        if( pRequestor != NULL )
        {
            /* get the handle for the variable being requested */
            pVarClient->rr.variableInfo.hVar = pRequestor->rr.variableInfo.hVar;

            /* get the PID of the client requesting the print */
            pVarClient->rr.peer_pid = pRequestor->rr.client_pid;

            /* get the PID of the client performing the print */
            pRequestor->rr.peer_pid = pVarClient->rr.client_pid;

            /* unblock the requesting client */
            UnblockClient( pRequestor );

            /* indicate success */
            result = EOK;
        }
        else
        {
            /* print session transaction was not found */
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestClosePrintSession                                        */
/*!
    Handle a request to close a print session

    The ProcessVarRequestClosePrintSession function handles a request
    from a client to close a print session.

    The client which is requesting the variable to be printed is unblocked
    by this function.

    @param[in]
        pVarClient
            Pointer to the client data structure of the client closing
            the print session.

    @retval EOK the print session was closed
    @retval ENOENT the print session was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ProcessVarRequestClosePrintSession( VarClient *pVarClient )
{
    int result = EINVAL;
    VarClient *pRequestor;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* get the client object of the requestor of the print */
        pRequestor = (VarClient *)TRANSACTION_Remove( pVarClient->rr.requestVal );
        if( pRequestor != NULL )
        {
            /* unblock the reqeusting client */
            UnblockClient( pRequestor );

            result = EOK;
        }
        else
        {
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestSet                                                      */
/*!
    Process a SET variable request from a client

    The ProcessVarRequestSet function handles a "SET variable" request
    from a client.  It populates the variable value into the varstorage
    for the variable

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable value was set
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ProcessVarRequestSet( VarClient *pVarClient )
{
    int result = EINVAL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* check if we are dealing with a string */
        if( pVarClient->rr.variableInfo.var.type == VARTYPE_STR )
        {
            /* for strings, the data is transferred via the working buffer */
            pVarClient->rr.variableInfo.var.val.str = &pVarClient->workbuf;
        }

        /* check if we are dealing with a blob */
        if( pVarClient->rr.variableInfo.var.type == VARTYPE_BLOB )
        {
            /* for strings, the data is transferred via the working buffer */
            pVarClient->rr.variableInfo.var.val.blob = &pVarClient->workbuf;
        }

        /* set the variable value */
        result = VARLIST_Set( pVarClient->rr.client_pid,
                              &pVarClient->rr.variableInfo,
                              &pVarClient->validationInProgress,
                              (void *)pVarClient );

        pVarClient->rr.responseVal = result;
    }

    if( ( result != EOK ) &&
        ( pVarClient->debug >= LOG_DEBUG ) )
    {
        printf( "SERVER: %s result = %s\n", __func__, strerror( result ) );
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestType                                                     */
/*!
    Process a TYPE variable request from a client

    The ProcessVarRequestType function handles a "variable TYPE" request
    from a client.  It requests the variable type for the specified
    variable

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable type was retrieved
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ProcessVarRequestType( VarClient *pVarClient )
{
    int result = EINVAL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* fet the variable type */
        result = VARLIST_GetType( &pVarClient->rr.variableInfo );
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestName                                                     */
/*!
    Process a NAME variable request from a client

    The ProcessVarRequestName function handles a "variable NAME" request
    from a client.  It requests the variable name for the specified
    variable

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable type was retrieved
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ProcessVarRequestName( VarClient *pVarClient )
{
    int result = EINVAL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* fetch the variable name */
        result = VARLIST_GetName( &pVarClient->rr.variableInfo );
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestLength                                                   */
/*!
    Process a LENGTH variable request from a client

    The ProcessVarRequestLength function handles a "variable LENGTH" request
    from a client.  It requests the variable length for the specified
    variable.  This is mostly useful for variable length data types such
    as strings.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable length was retrieved
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ProcessVarRequestLength( VarClient *pVarClient )
{
    int result = EINVAL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* get the variable length */
        result = VARLIST_GetLength( &(pVarClient->rr.variableInfo) );
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestNotify                                                   */
/*!
    Process a NOTIFY variable request from a client

    The ProcessVarRequestNotify function handles a variable notification
    requests from a client.  It registers a request for a notification
    of type:
        NOTIFY_MODIFIED_QUEUE
            - request a notification when the notification queue changes
        NOTIFY_MODIFIED
            - request a notification when a variable's value changes
        NOTIFY_CALC
            - request a notification when a variable's value is requested
              by another client
        NOTIFY_VALIDATE
            - request a notification when a variable is being changed
              by another client, before the change is applied.
        NOTIFY_PRINT
            - request a notification when a client tries to print
              the variable with the given handle

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable notification was successfully registered
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ProcessVarRequestNotify( VarClient *pVarClient )
{
    int result = EINVAL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* register the notification request */
        result = VARLIST_RequestNotify( &(pVarClient->rr.variableInfo),
                                        pVarClient->rr.client_pid );
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestGet                                                      */
/*!
    Process a GET variable request from a client

    The ProcessVarRequestGet function handles a "GET variable" request
    from a client.  It populates the variable type, value and length
    into the client's VarInfo object.  If the variable is a string,
    the string is transferred into the client'sworking buffer.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable info was returned
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ProcessVarRequestGet( VarClient *pVarClient )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        result = VARLIST_GetByHandle( pVarClient->rr.client_pid,
                                      &pVarClient->rr.variableInfo,
                                      &pVarClient->workbuf,
                                      pVarClient->workbufsize );
        if( result == EINPROGRESS )
        {
            /* add the client to the blocked clients list */
            BlockClient( pVarClient, NOTIFY_CALC );
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestGetFirst                                                 */
/*!
    Process a GET_FIRST variable request from a client

    The ProcessVarRequestGetFirst function handles a "GET_FIRST variable"
    request from a client.  It initiates a search for variables which
    match the specified search criteria.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable info was returned
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ProcessVarRequestGetFirst( VarClient *pVarClient )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        result = VARLIST_GetFirst( pVarClient->rr.client_pid,
                                   pVarClient->rr.requestVal,
                                   &pVarClient->rr.variableInfo,
                                   &pVarClient->workbuf,
                                   pVarClient->workbufsize,
                                   &pVarClient->rr.responseVal);
        if( result == EINPROGRESS )
        {
            /* add the client to the blocked clients list */
            BlockClient( pVarClient, NOTIFY_CALC );
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestGetNext                                                  */
/*!
    Process a GET_NEXT variable request from a client

    The ProcessVarRequestGetNext function handles a "GET_NEXT variable"
    request from a client.  It continues a search for variables which
    match the specified search criteria.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable info was returned
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ProcessVarRequestGetNext( VarClient *pVarClient )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        result = VARLIST_GetNext( pVarClient->rr.client_pid,
                                  pVarClient->rr.requestVal,
                                  &pVarClient->rr.variableInfo,
                                  &pVarClient->workbuf,
                                  pVarClient->workbufsize,
                                  &pVarClient->rr.responseVal );
        if( result == EINPROGRESS )
        {
            /* add the client to the blocked clients list */
            BlockClient( pVarClient, NOTIFY_CALC );
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessValidationRequest                                                  */
/*!
    Process a Validation Request from a client

    The ProcessValidationRequest function handles a validation request
    from a client.  A client sends a validation request to the server
    after it has received a validation signal from the server indicating
    that another client is trying to change a variable for which the
    client is registered as a validator.

    The Validation Request is used to retrieve the pending new value
    so it can be validated by the client.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the validation information was returned
    @retval ENOENT the validation request was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ProcessValidationRequest( VarClient *pVarClient )
{
    int result = EINVAL;
    VarInfo *pVarInfo;
    pid_t peer_pid;
    VarClient *pSetClient;

    /* validated the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK )
    {
        /* get a pointer to the client requesting the validation */
        pSetClient = (VarClient *)TRANSACTION_Get( pVarClient->rr.requestVal );
        if( pSetClient != NULL )
        {
            if( pSetClient->rr.variableInfo.var.type == VARTYPE_STR )
            {
                /* strings will be stored in the client's working buffer */
                pVarClient->rr.variableInfo.var.val.str = &pVarClient->workbuf;
                pVarClient->rr.variableInfo.var.len = pVarClient->workbufsize;
            }

            if( pSetClient->rr.variableInfo.var.type == VARTYPE_BLOB )
            {
                /* blobs will be stored in the client's working buffer */
                pVarClient->rr.variableInfo.var.val.blob = &pVarClient->workbuf;


                // TO DO : Need to pass blob size as well as blob!!!!
                pVarClient->rr.variableInfo.var.len = pVarClient->workbufsize;
            }

            /* copy the variable handle from the setting client to the
               validating client */
            pVarClient->rr.variableInfo.hVar = pSetClient->rr.variableInfo.hVar;

            /* copy the Variable object from the setter to the validator */
            result = VAROBJECT_Copy( &pVarClient->rr.variableInfo.var,
                                     &pSetClient->rr.variableInfo.var );
        }
        else
        {
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessValidationResponse                                                 */
/*!
    Process a Validation Response from a client

    The ProcessValidationResponse function handles a validge is accepted
    or rejected.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the validation information was returned
    @retval ENOENT the validation request was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
static int ProcessValidationResponse( VarClient *pVarClient )
{
    int result = EINVAL;
    VarInfo *pVarInfo;
    VarClient *pSetClient;

    pid_t peer_pid;

    /* validated the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK )
    {
        /* get a pointer to the client requesting the validation */
        pSetClient = (VarClient *)TRANSACTION_Remove(pVarClient->rr.requestVal);
        if( pSetClient != NULL )
        {
            /* copy the response from the validator to the setter */
            pSetClient->rr.responseVal = pVarClient->rr.responseVal;

            if( pVarClient->rr.responseVal == EOK )
            {
                /* set the value on behalf of the requestor */
                result = ProcessVarRequestSet( pSetClient );
            }

            /* unblock the client */
            UnblockClient( pSetClient );
        }
    }

    return result;

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
    VAR_HANDLE hVar;
    VarObject *pVarObject;
    size_t len;
    size_t n;
    size_t i;
    char *pMetricName;
    uint64_t *pMetric;

    /* initialize an empty stats object */
    STATS_Initialize();

    /* set the transactions per second variable */
    STATS_SetRequestsPerSecPtr(MakeMetric("/varserver/stats/tps" ));
    STATS_SetTotalRequestsPtr(MakeMetric("/varserver/stats/transactions"));

    /* set up the blocked client counter metric */
    SetBlockedClientMetric(MakeMetric("/varserver/stats/blocked_clients"));

    /* create the metric variable */
    memset(&info, 0, sizeof(VarInfo));
    len = sizeof(info.name);
    strncpy(info.name, "/varserver/client/info", len);
    info.name[len-1] = 0;
    info.var.len = BUFSIZ;
    info.var.val.str = calloc(1, BUFSIZ);
    info.var.type = VARTYPE_STR;
    VARLIST_AddNew( &info, &hClientInfo );

    /* make metrics for all the request endpoints */
    n = sizeof(RequestHandlers) / sizeof(RequestHandler);
    for ( i=0; i<n; i++ )
    {
        pMetricName = RequestHandlers[i].pMetricName;
        if( pMetricName != NULL )
        {
            pMetric = MakeMetric( pMetricName );
            RequestHandlers[i].pMetric = pMetric;
        }
    }

    return EOK;
}

/*============================================================================*/
/*  MakeMetric                                                                */
/*!
    Create a 64-bit metric counter

    The MakeMetric function creates a new 64-bit unsigned integer for
    use as a metric counter.  It returns a pointer to the 64 bit value
    that can be quickly accessed to update the metric value.

    @param[in]
        name
            pointer to the name of the metric to create

    @retval pointer to the 64-bit metric value
    @retval NULL if the metric could not be created

==============================================================================*/
static uint64_t *MakeMetric( char *name )
{
    uint64_t *p = NULL;
    VarInfo info;
    VAR_HANDLE hVar;
    VarObject *pVarObject;
    size_t len;

    /* create the metric variable */
    memset(&info, 0, sizeof(VarInfo));
    len = sizeof(info.name);
    strncpy(info.name, name, len);
    info.name[len-1] = 0;
    info.var.len = sizeof( uint64_t );
    info.var.type = VARTYPE_UINT64;
    VARLIST_AddNew( &info, &hVar );

    /* get a pointer to the metric variable */
    pVarObject = VARLIST_GetObj( hVar );
    if ( pVarObject != NULL )
    {
        /* get a pointer to the metric value */
        p = &(pVarObject->val.ull);
    }

    return p;
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
static int PrintClientInfo( VarInfo *pVarInfo, char *buf, size_t len )
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

/*! @}
 * end of varserver group */
