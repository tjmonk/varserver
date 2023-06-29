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
#include "server.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*! Maximum number of clients */
#define MAX_VAR_CLIENTS                 ( 256 )

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

    /*! the name of the request */
    char *requestName;

    /*! pointer to the request handler function */
    int (*handler)( VarClient *pVarClient );

    /*! counter for the number of times this request has been made */
    uint32_t requestCount;

} RequestHandler;

/*! the RequestStats is used to track the total number of requests
    handled by the variable server, as well as the number of
    requests per second */
typedef struct _RequestStats
{
    /*! track the number of requests */
    size_t requestCount;

    /*! total request count */
    size_t totalRequestCount;

    /*! Track the number of requests per second */
    size_t requestsPerSec;

    /*! handle to the varserver transaction count statistic */
    VAR_HANDLE hTransactionCount;

    /*! handle to the varserver transactions per second statistic */
    VAR_HANDLE hTransactionsPerSecond;
} RequestStats;

/*==============================================================================
        Private function declarations
==============================================================================*/
static int NewClient( pid_t pid );
static int ProcessRequest( siginfo_t *pInfo, RequestStats *pStats );
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


static int InitStats( RequestStats *pStats );
static void CreateStatsTimer( int timeoutms );
static void ProcessStats( RequestStats *pStats );

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! variable clients */
static VarClient *VarClients[MAX_VAR_CLIENTS] = {0};
static ServerInfo *pServerInfo = NULL;

/*! Request Handlers - these must appear in the exact same order
    as the request enumerations so they can be looked up directly
    in the Request array */
static RequestHandler RequestHandlers[] =
{
    {
        VARREQUEST_INVALID,
        "INVALID",
        ProcessVarRequestInvalid,
        0
    },
    {
        VARREQUEST_OPEN,
        "OPEN",
        NULL,
        0
    },
    {
        VARREQUEST_CLOSE,
        "CLOSE",
        ProcessVarRequestClose,
        0
    },
    {
        VARREQUEST_ECHO,
        "ECHO",
        ProcessVarRequestEcho,
        0
    },
    {
        VARREQUEST_NEW,
        "NEW",
        ProcessVarRequestNew,
        0
    },
    {
        VARREQUEST_FIND,
        "FIND",
        ProcessVarRequestFind,
        0
    },
    {
        VARREQUEST_GET,
        "GET",
        ProcessVarRequestGet,
        0
    },
    {
        VARREQUEST_PRINT,
        "PRINT",
        ProcessVarRequestPrint,
        0
    },
    {
        VARREQUEST_SET,
        "SET",
        ProcessVarRequestSet,
        0
    },
    {
        VARREQUEST_TYPE,
        "TYPE",
        ProcessVarRequestType,
        0
    },
    {
        VARREQUEST_NAME,
        "NAME",
        ProcessVarRequestName,
        0
    },
    {
        VARREQUEST_LENGTH,
        "LENGTH",
        ProcessVarRequestLength,
        0
    },
    {
        VARREQUEST_NOTIFY,
        "NOTIFY",
        ProcessVarRequestNotify,
        0
    },
    {
        VARREQUEST_GET_VALIDATION_REQUEST,
        "VALIDATION_REQUEST",
        ProcessValidationRequest,
        0
    },
    {
        VARREQUEST_SEND_VALIDATION_RESPONSE,
        "VALIDATION_RESPONSE",
        ProcessValidationResponse,
        0
    },
    {
        VARREQUEST_OPEN_PRINT_SESSION,
        "OPEN_PRINT_SESSION",
        ProcessVarRequestOpenPrintSession,
        0
    },
    {
        VARREQUEST_CLOSE_PRINT_SESSION,
        "CLOSE_PRINT_SESSION",
        ProcessVarRequestClosePrintSession,
        0
    },
    {
        VARREQUEST_GET_FIRST,
        "GET_FIRST",
        ProcessVarRequestGetFirst,
        0
    },
    {
        VARREQUEST_GET_NEXT,
        "GET_FIRST",
        ProcessVarRequestGetNext,
        0
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
    RequestStats stats;

    printf("SERVER: pid=%d\n", getpid());


    sigemptyset(&mask);
    sigaddset(&mask, SIG_NEWCLIENT );
    sigaddset(&mask, SIG_CLIENT_REQUEST );
    sigaddset(&mask, SIG_TIMER );
    sigprocmask(SIG_BLOCK, &mask, NULL );

    /* Set up server information structure */
    pServerInfo = InitServerInfo();
    if( pServerInfo != NULL )
    {
        InitStats( &stats );

        while(1)
        {
            memset( &info, 0, sizeof( siginfo_t ) );
            signum = sigwaitinfo(&mask, &info);
            if ( signum == SIG_NEWCLIENT )
            {
                NewClient( info.si_pid );
            }
            else if ( signum == SIG_CLIENT_REQUEST )
            {
                ProcessRequest( &info, &stats );
            }
            else if ( signum == SIG_TIMER )
            {
                ProcessStats( &stats );
            }
            else
            {
                /* should not get here */
            }
        }
    }

    printf("SERVER: exiting!\n");
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
static int ProcessRequest( siginfo_t *pInfo, RequestStats *pStats )
{
    int result = EINVAL;
    int clientid;
    VarClient *pVarClient;
    VarRequest requestType;
    int (*handler)(VarClient *pVarClient);

    if ( ( pInfo != NULL ) &&
         ( pStats != NULL ) )
    {
        /* get the client id */
        clientid = pInfo->si_value.sival_int;

        /* update the request stats */
        pStats->requestCount++;
        pStats->totalRequestCount++;

        if( clientid < MAX_VAR_CLIENTS )
        {
            /* get a pointer to the client information object */
            pVarClient = VarClients[clientid];
            if( pVarClient != NULL )
            {
                if( pVarClient->requestType >= VARREQUEST_END_MARKER )
                {
                    requestType = VARREQUEST_INVALID;
                }
                else
                {
                    requestType = pVarClient->requestType;
                }

                if( pVarClient->debug >= LOG_DEBUG )
                {
                    printf("SERVER: Processing request %s from client %d\n",
                            RequestHandlers[requestType].requestName,
                            clientid);
                }

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

        pVarClient->responseVal = 0;

        /* allow the client to proceed */
        UnblockClient( pVarClient );

        /* get the client id */
        clientid = pVarClient->clientid;

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
                    pVarClient->clientid,
                    pVarClient->client_pid );
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
            VarClients[clientId] = pVarClient;
            pVarClient->clientid = clientId;

            /* close the file descriptor since we don't need it for anything */
            close( fd );

            UnblockClient( pVarClient );
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
        ( pVarClient->id == VARSERVER_ID ) )
    {
        if( pVarClient->version == VARSERVER_VERSION )
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
        if( pVarClient->variableInfo.var.type == VARTYPE_STR )
        {
            pVarClient->variableInfo.var.val.str = &pVarClient->workbuf;
        }

        /* special handling for blob variables uses the client's
           working buffer to pass the variable value */
        if( pVarClient->variableInfo.var.type == VARTYPE_BLOB )
        {
            pVarClient->variableInfo.var.val.blob = &pVarClient->workbuf;
        }

        /* add the new variable to the variable list */
        rc = VARLIST_AddNew( &pVarClient->variableInfo, &varhandle );
        if( rc == EOK )
        {
            /* return the new variable's handle */
            pVarClient->responseVal = varhandle;
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
        result = VARLIST_Find( &pVarClient->variableInfo,
                               (VAR_HANDLE *)&pVarClient->responseVal );
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
        pVarClient->responseVal = pVarClient->requestVal;
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
        result = VARLIST_PrintByHandle( pVarClient->client_pid,
                                        &pVarClient->variableInfo,
                                        &pVarClient->workbuf,
                                        pVarClient->workbufsize,
                                        pVarClient,
                                        &handler );

        /* capture the result */
        pVarClient->responseVal = result;

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
            pVarClient->peer_pid = handler;

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
        pRequestor = (VarClient *)TRANSACTION_Get( pVarClient->requestVal );
        if( pRequestor != NULL )
        {
            /* get the handle for the variable being requested */
            pVarClient->variableInfo.hVar = pRequestor->variableInfo.hVar;

            /* get the PID of the client requesting the print */
            pVarClient->peer_pid = pRequestor->client_pid;

            /* get the PID of the client performing the print */
            pRequestor->peer_pid = pVarClient->client_pid;

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
        pRequestor = (VarClient *)TRANSACTION_Remove( pVarClient->requestVal );
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
        if( pVarClient->variableInfo.var.type == VARTYPE_STR )
        {
            /* for strings, the data is transferred via the working buffer */
            pVarClient->variableInfo.var.val.str = &pVarClient->workbuf;
        }

        /* check if we are dealing with a blob */
        if( pVarClient->variableInfo.var.type == VARTYPE_BLOB )
        {
            /* for strings, the data is transferred via the working buffer */
            pVarClient->variableInfo.var.val.blob = &pVarClient->workbuf;
        }

        /* set the variable value */
        result = VARLIST_Set( pVarClient->client_pid,
                              &pVarClient->variableInfo,
                              &pVarClient->validationInProgress,
                              (void *)pVarClient );

        pVarClient->responseVal = result;
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
        result = VARLIST_GetType( &pVarClient->variableInfo );
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
        result = VARLIST_GetName( &pVarClient->variableInfo );
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
        result = VARLIST_GetLength( &(pVarClient->variableInfo) );
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
        result = VARLIST_RequestNotify( &(pVarClient->variableInfo),
                                        pVarClient->client_pid );
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
        result = VARLIST_GetByHandle( pVarClient->client_pid,
                                      &pVarClient->variableInfo,
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
        result = VARLIST_GetFirst( pVarClient->client_pid,
                                   pVarClient->requestVal,
                                   &pVarClient->variableInfo,
                                   &pVarClient->workbuf,
                                   pVarClient->workbufsize,
                                   &pVarClient->responseVal);
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
        result = VARLIST_GetNext( pVarClient->client_pid,
                                  pVarClient->requestVal,
                                  &pVarClient->variableInfo,
                                  &pVarClient->workbuf,
                                  pVarClient->workbufsize,
                                  &pVarClient->responseVal );
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
        pSetClient = (VarClient *)TRANSACTION_Get( pVarClient->requestVal );
        if( pSetClient != NULL )
        {
            if( pSetClient->variableInfo.var.type == VARTYPE_STR )
            {
                /* strings will be stored in the client's working buffer */
                pVarClient->variableInfo.var.val.str = &pVarClient->workbuf;
                pVarClient->variableInfo.var.len = pVarClient->workbufsize;
            }

            if( pSetClient->variableInfo.var.type == VARTYPE_BLOB )
            {
                /* blobs will be stored in the client's working buffer */
                pVarClient->variableInfo.var.val.blob = &pVarClient->workbuf;


                // TO DO : Need to pass blob size as well as blob!!!!
                pVarClient->variableInfo.var.len = pVarClient->workbufsize;
            }

            /* copy the variable handle from the setting client to the
               validating client */
            pVarClient->variableInfo.hVar = pSetClient->variableInfo.hVar;

            /* copy the Variable object from the setter to the validator */
            result = VAROBJECT_Copy( &pVarClient->variableInfo.var,
                                     &pSetClient->variableInfo.var );
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

    The ProcessValidationResponse function handles a validation response
    from a client.  A client sends a validation response to the server
    after it has completed a validation request and is ready to
    indicate if the data point change is accepted or rejected.

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
        pSetClient = (VarClient *)TRANSACTION_Remove(pVarClient->requestVal);
        if( pSetClient != NULL )
        {
            /* copy the response from the validator to the setter */
            pSetClient->responseVal = pVarClient->responseVal;

            if( pVarClient->responseVal == EOK )
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
    and sets up the statistics calculation timer.

    @param[in]
        pStats
            pointer to the RequestStats statistics object

    @retval EOK - stats initialized ok
    @retval EINVAL - invalid arguments

==============================================================================*/
int InitStats( RequestStats *pStats )
{
    int result = EINVAL;
    VarInfo info;
    VAR_HANDLE hVar;

    if ( pStats != NULL )
    {
        /* reset the request stats */
        memset( pStats, 0, sizeof( RequestStats ) );

        /* set the transactions per second variable */
        memset(&info, 0, sizeof(VarInfo));
        strcpy(info.name, "/varserver/stats/tps");
        info.var.len = sizeof( uint32_t );
        info.var.type = VARTYPE_UINT32;
        VARLIST_AddNew( &info, &pStats->hTransactionsPerSecond );

        /* set the total transactions variable */
        strcpy(info.name, "/varserver/stats/transactions");
        info.var.len = sizeof( uint64_t );
        info.var.type = VARTYPE_UINT64;
        VARLIST_AddNew( &info, &pStats->hTransactionCount );

        /* set up the statistics timer */
        CreateStatsTimer( 1000 );

        result = EOK;
    }

    return result;
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
    int sigNo = SIGRTMIN+5;
    long secs;
    long msecs;

    timer_t timerID;
    int result = EINVAL;

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

/*============================================================================*/
/*  ProcessStats                                                              */
/*!
    Process the statistics

    The ProcessStats function processes the varserver statistics once
    every timer interval (1 second)

@param[in]
    pStats
        pointer to the stats

==============================================================================*/
static void ProcessStats( RequestStats *pStats )
{
    VarInfo info;
    bool validationInProgress;

    if ( pStats != NULL )
    {
        /* update the requests per second counter */
        pStats->requestsPerSec = pStats->requestCount;
        pStats->requestCount=0;

        /* set the transactions per second variable */
        memset(&info, 0, sizeof(VarInfo));
        info.hVar = pStats->hTransactionCount;
        info.var.val.ull = pStats->totalRequestCount;
        info.var.len = sizeof( uint64_t );
        info.var.type = VARTYPE_UINT64;
        VARLIST_Set( -1, &info, &validationInProgress, NULL );
        info.hVar = pStats->hTransactionsPerSecond;
        info.var.val.ul = pStats->requestsPerSec;
        info.var.len = sizeof( uint32_t );
        info.var.type = VARTYPE_UINT32;
        VARLIST_Set( -1, &info, &validationInProgress, NULL );
    }
}

/*! @}
 * end of varserver group */
