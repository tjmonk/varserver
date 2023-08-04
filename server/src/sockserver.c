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
 * @defgroup sockserver sockserver
 * @brief Variable Server Socket Interface
 * @{
 */

/*============================================================================*/
/*!
@file sockserver.c

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
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include <varserver/varclient.h>
#include <varserver/varserver.h>
#include <varserver/sockapi.h>
#include "varlist.h"
#include "taglist.h"
#include "blocklist.h"
#include "transaction.h"
#include "stats.h"
#include "clientlist.h"
#include "metric.h"
#include "sockserver.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*! varserver listening port (VS = 0x5653) */
#define PORT 22099

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
    int (*handler)( VarClient *pVarClient, SockRequest *pReq );

    /*! pointer to the name of the metric counter */
    char *pMetricName;

    /*! counter for the number of times this request has been made */
    uint64_t *pMetric;

} RequestHandler;

/*! The RenderHandler object maps a Var Server variable handle to a
    function which will render its output */
typedef struct renderHandler
{
    /* handle to the variable to be rendered */
    VAR_HANDLE hVar;

    /*! pointer to the render handling function */
    int (*fn)(VarInfo *pVarInfo, char *buf, size_t len);

    /*! pointer to the next render handler in the list */
    struct renderHandler *pNext;
} RenderHandler;

/*==============================================================================
        Private function declarations
==============================================================================*/

static int SetupListener( void );
static int HandleNewClient( int sock, fd_set *pfds );
static int HandleClientRequest( int sock, fd_set *pfds );
static int SendClientResponse( VarClient *pVarClient );
static int ReadPayload( int sd, VarClient *pVarClient );

static int writesd( int sd, char *p, size_t len );
static int readsd( int sd, char *p, size_t len );

static int InitHandlerMetrics( void );
static int ProcessRequest( VarClient *pVarClient, SockRequest *pReq );
static int ValidateClient( VarClient *pVarClient );

static int ProcessVarRequestInvalid( VarClient *pVarClient, SockRequest *pReq );
static int ProcessVarRequestOpen( VarClient *pVarClient, SockRequest *pReq );
static int ProcessVarRequestClose( VarClient *pVarClient, SockRequest *pReq );
static int ProcessVarRequestEcho( VarClient *pVarClient, SockRequest *pReq );
static int ProcessVarRequestNew( VarClient *pVarClient, SockRequest *pReq );
static int ProcessVarRequestFind( VarClient *pVarClient,SockRequest *pReq );
static int ProcessVarRequestPrint( VarClient *pVarClient, SockRequest *pReq );
static int ProcessVarRequestSet( VarClient *pVarClient, SockRequest *pReq );
static int ProcessVarRequestType( VarClient *pVarClient, SockRequest *pReq );
static int ProcessVarRequestName( VarClient *pVarClient, SockRequest *pReq );
static int ProcessVarRequestLength( VarClient *pVarClient, SockRequest *pReq );
static int ProcessVarRequestGet( VarClient *pVarClient, SockRequest *pReq );
static int ProcessVarRequestNotify( VarClient *pVarClient, SockRequest *pReq );
static int ProcessValidationRequest( VarClient *pVarClient, SockRequest *pReq );
static int ProcessValidationResponse( VarClient *pVarClient,
                                      SockRequest *pReq );
static int ProcessVarRequestOpenPrintSession( VarClient *pVarClient,
                                              SockRequest *pReq );
static int ProcessVarRequestClosePrintSession( VarClient *pVarClient,
                                               SockRequest *pReq );
static int ProcessVarRequestGetFirst( VarClient *pVarClient,
                                      SockRequest *pReq );
static int ProcessVarRequestGetNext( VarClient *pVarClient,
                                     SockRequest *pReq );

static int AddRenderHandler( VAR_HANDLE hVar,
                             int (*fn)( VarInfo *pVarInfo,
                                        char *buf,
                                        size_t len) );

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! pointer to a list of render handlers */
static RenderHandler *pRenderHandlers = NULL;

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
        ProcessVarRequestOpen,
        NULL,
        NULL
    },
    {
        VARREQUEST_CLOSE,
        "CLOSE",
        ProcessVarRequestClose,
        "/varserver/sock/close",
        NULL
    },
    {
        VARREQUEST_ECHO,
        "ECHO",
        ProcessVarRequestEcho,
        "/varserver/sock/echo",
        NULL
    },
    {
        VARREQUEST_NEW,
        "NEW",
        ProcessVarRequestNew,
        "/varserver/sock/new",
        NULL
    },
    {
        VARREQUEST_FIND,
        "FIND",
        ProcessVarRequestFind,
        "/varserver/sock/find",
        NULL
    },
    {
        VARREQUEST_GET,
        "GET",
        ProcessVarRequestGet,
        "/varserver/sock/get",
        NULL
    },
    {
        VARREQUEST_PRINT,
        "PRINT",
        ProcessVarRequestPrint,
        "/varserver/sock/print",
        NULL
    },
    {
        VARREQUEST_SET,
        "SET",
        ProcessVarRequestSet,
        "/varserver/sock/set",
        NULL
    },
    {
        VARREQUEST_TYPE,
        "TYPE",
        ProcessVarRequestType,
        "/varserver/sock/type",
        NULL
    },
    {
        VARREQUEST_NAME,
        "NAME",
        ProcessVarRequestName,
        "/varserver/sock/name",
        NULL
    },
    {
        VARREQUEST_LENGTH,
        "LENGTH",
        ProcessVarRequestLength,
        "/varserver/sock/length",
        NULL
    },
    {
        VARREQUEST_NOTIFY,
        "NOTIFY",
        ProcessVarRequestNotify,
        "/varserver/sock/notify",
        NULL
    },
    {
        VARREQUEST_GET_VALIDATION_REQUEST,
        "VALIDATION_REQUEST",
        ProcessValidationRequest,
        "/varserver/sock/validate_request",
        NULL
    },
    {
        VARREQUEST_SEND_VALIDATION_RESPONSE,
        "VALIDATION_RESPONSE",
        ProcessValidationResponse,
        "/varserver/sock/validation_response",
        NULL
    },
    {
        VARREQUEST_OPEN_PRINT_SESSION,
        "OPEN_PRINT_SESSION",
        ProcessVarRequestOpenPrintSession,
        "/varserver/sock/open_print_session",
        NULL
    },
    {
        VARREQUEST_CLOSE_PRINT_SESSION,
        "CLOSE_PRINT_SESSION",
        ProcessVarRequestClosePrintSession,
        "/varserver/sock/close_print_session",
        NULL
    },
    {
        VARREQUEST_GET_FIRST,
        "GET_FIRST",
        ProcessVarRequestGetFirst,
        "/varserver/sock/get_first",
        NULL
    },
    {
        VARREQUEST_GET_NEXT,
        "GET_NEXT",
        ProcessVarRequestGetNext,
        "/varserver/sock/get_next",
        NULL
    }
};


/*==============================================================================
        Public function definitions
==============================================================================*/

/*============================================================================*/
/*  SOCKSERVER_Run                                                            */
/*!
    Run the Variable Server socket interface

    The SOCKSERVER_Run function runs the variable server socket interface.
    It waits for a socket message and handles it.

    @param[in]
        sock
            socket interface to process

    @retval EOK a transaction was successfully processed
    @retval EINVAL invalid arguments
    @retval EINTR interrupted by signal
    @retval other error from select

==============================================================================*/
int SOCKSERVER_Run( int sock )
{
    fd_set readfds;
    static fd_set savefds;
    static int clientcount = -1;
    int max_sd;
    int activity;
    int numclients;
    int result = EINVAL;

    if ( sock != -1 )
    {
        if ( clientcount == -1 )
        {
            /* one time initialization */
            FD_ZERO( &savefds );
            FD_SET( sock, &savefds );
            max_sd = sock;
        }

        readfds = savefds;
        numclients = GetActiveClients();
        if ( numclients != clientcount )
        {
            FD_ZERO( &readfds );
            FD_SET( sock, &readfds );
            max_sd = GetClientfds( sock, &readfds );
            savefds = readfds;
            clientcount = numclients;
        }

        /* wait for an activity on one of the sockets */
        activity = select( max_sd + 1 , &readfds , NULL , NULL , NULL);
        if ( activity > 0 )
        {
            /* handle new client connections */
            HandleNewClient( sock, &readfds );

            /* handle client requests */
            HandleClientRequest( sock, &readfds );

            result = EOK;
        }
        else if ( ( activity < 0 ) && ( errno!=EINTR ) )
        {
            printf("select error: %s\n", strerror(errno));
            result = errno;
        }
        else
        {
            result = errno;
        }
    }

    return result;
}

int SOCKSERVER_Init( void )
{
    int sock = -1;
    int opt = true;
    int rc;
    int addrlen;
    struct sockaddr_in address;

    InitHandlerMetrics();

    /* create a master socket */
    sock = socket(AF_INET , SOCK_STREAM , 0);
    if ( sock > 0 )
    {
        /* set the socket to allow multiple connections */
        rc = setsockopt( sock,
                         SOL_SOCKET,
                         SO_REUSEADDR,
                         (char *)&opt,
                         sizeof(opt));

        if ( rc >= 0 )
        {
            /* set the socket type */
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = INADDR_ANY;
            address.sin_port = htons( PORT );

            /* bind the socket to localhost port */
            rc = bind( sock, (struct sockaddr *)&address, sizeof(address));
            if ( rc >= 0 )
            {
                /* try to specify maximum of 3 pending connections on
                   then input socket */
                rc = listen( sock, 3 );
                if ( rc != 0 )
                {
                    printf("listen failed\n");
                    close( sock );
                    sock = -1;
                }
            }
            else
            {
                printf("bind failed\n");
                close( sock );
                sock = -1;
            }
        }
        else
        {
            printf("setsockopt failed");
            close( sock );
            sock = -1;
        }

    }
    else
    {
        printf( "cannot create socket");
        sock = -1;
    }

    return sock;
}

static int HandleNewClient( int sock, fd_set *pfds )
{
    int addrlen = sizeof( struct sockaddr_in );
    struct sockaddr_in address;
    int new_client;
    ssize_t n;
    ssize_t s;
    int result = EINVAL;
    VarClient *pVarClient;
    int clientid = -1;

    if ( ( sock != -1 ) && ( pfds != NULL ) )
    {
        result = EOK;

        /* If something happened on the listening socket then it is an
           incoming connection */
        if ( FD_ISSET( sock, pfds ) )
        {
            new_client = accept( sock,
                                 (struct sockaddr *)&address,
                                 (socklen_t *)&addrlen );
            if ( new_client > 0 )
            {
                /* add the new client */
                pVarClient = NewClient( new_client );
                if ( pVarClient != NULL )
                {
                    clientid = pVarClient->rr.clientid;
                }

                /* report new client connection */
                printf( "New connection: "
                        "id: %d, "
                        "fd : %d, "
                        "ip : %s, "
                        "port : %d\n",
                        clientid,
                        new_client,
                        inet_ntoa(address.sin_addr),
                        ntohs(address.sin_port));
            }
            else
            {
                printf("accept: %s\n", strerror(errno));
            }
        }
    }

    return result;
}

static int HandleClientRequest( int sock, fd_set *pfds )
{
    int result = EINVAL;
    int sd;
    char buffer[1025];  //data buffer of 1K
    ssize_t n;
    struct sockaddr_in address;
    int addrlen;
    int i = 0;
    int *pSDMap = GetClientSDMap();
    VarClient *pVarClient;
    int clientid;
    SockRequest req;

    if ( pfds != NULL )
    {
        result = EOK;

        do
        {
            /* get socket descriptor from sd map */
            sd = pSDMap[i];
            if ( sd > 0 )
            {
                /* check if socket has data ready to read */
                if ( FD_ISSET( sd, pfds ) )
                {
                    /* get pointer to the Variable client */
                    pVarClient = GetClient(i);
                    clientid = ( pVarClient != NULL ) ? pVarClient->rr.clientid
                                                      : -1;

                    /* read incoming message */
                    n = read( sd, &req, sizeof(SockRequest) );
                    if ( n == 0 )
                    {
                        /* client disconnected */
                        getpeername( sd,
                                    (struct sockaddr *)&address,
                                    (socklen_t *)&addrlen );

                        printf( "Client disconnected: "
                                "id: %d, "
                                "ip: %s, "
                                "port: %d\n",
                                clientid,
                                inet_ntoa(address.sin_addr),
                                ntohs(address.sin_port) );

                        /* close and clear the client socket */
                        close( sd );

                        /* Delete the client and move it to the free list */
                        DeleteClient( pVarClient );
                    }
                    else if ( n != sizeof(SockRequest) )
                    {
                        printf("SERVER: Invalid read: n=%ld\n", n );
                    }
                    else if ( ( req.id == VARSERVER_ID ) &&
                              ( req.version == VARSERVER_VERSION ) )
                    {
                        ProcessRequest( pVarClient, &req );
                    }
                    else
                    {
                        printf("SERVER: Invalid read\n");
                    }
                }
            }

            /* move to next socket descriptor */
            i++;
        } while ( sd != 0 );
    }

    return result;
}

/*============================================================================*/
/*  SendClientResponse                                                        */
/*!
    Send a response from the server to the client

    The SendClientResponse function is used to send a client response
    from the Variable Server to one of its clients via a socket descriptor.

    If the request response length is greater than zero, then the
    variable length response in the client's working buffer is
    sent also.

    @param[in]
        pVarClient
            pointer to the VarClient object belonging to the client

    @retval EOK - the client response was handled successfully by the server
    @retval EINVAL - an invalid client was specified
    @retval other - error code

==============================================================================*/
static int SendClientResponse( VarClient *pVarClient )
{
    int result = EINVAL;
    int rc;
    char *p;
    size_t len;
    int sd;

    if( pVarClient != NULL )
    {
        if( pVarClient->debug >= LOG_DEBUG )
        {
            printf( "SERVER: Sending client response (%d)\n",
                    pVarClient->rr.responseVal );
        }

//        printf("SERVER: Send client response\n" );
//        printf("SERVER:     id: %d\n", pVarClient->rr.id );
//        printf("SERVER:     version: %d\n", pVarClient->rr.version);

        /* get the socket descriptor */
        sd = pVarClient->sd;

        /* send response */
        result = writesd( sd,
                          (char *)&pVarClient->rr,
                          sizeof( RequestResponse ) );
        if ( result == EOK )
        {
            /* check the length of the (optional) request response body */
            len = pVarClient->rr.len;
            if ( len > 0 )
            {
                /* write data from the working buffer */
//                printf("SERVER: Sending variable length response: %ld\n", len );
                result = writesd( sd, (char *)&pVarClient->workbuf, len );
            }
        }
    }

    if( ( result != EOK ) &&
        ( pVarClient->debug >= LOG_ERR ) )
    {
        printf("%s failed: (%d) %s\n", __func__, result, strerror(result));
    }

    if (result != EOK )
    {
        printf("%s failed: (%d) %s\n", __func__, result, strerror(result));
    }

    return result;
}

/*============================================================================*/
/*  writesd                                                                   */
/*!
    Write a buffer to a socket descriptor

    The writesd function is used to send a buffer of data to the
    server via a socket descriptor.

    @param[in]
        sd
            socket descriptor to send data on

    @param[in]
        p
            pointer to the data to send

    @param[in]
        len
            length of data to send

    @retval EOK - the data as sent successfully
    @retval EINVAL - invalid arguments
    @retval other - error code from write()

==============================================================================*/
static int writesd( int sd, char *p, size_t len )
{
    size_t n = 0;
    size_t sent = 0;
    size_t remaining = len;
    int result = EINVAL;

    if ( ( p != NULL ) &&
         ( len > 0 ) )
    {
        do
        {
            n = write( sd,
                    &p[sent],
                    remaining );
//            printf("SERVER: write: n=%ld\n", n);
            if ( n < 0 )
            {
                result = errno;
                if ( result != EINTR )
                {
                    break;
                }
            }
            else
            {
                sent += n;
                remaining -= n;
            }
        }
        while ( remaining > 0 );

        if ( remaining == 0 )
        {
            result = EOK;
        }
    }

    return result;
}

/*============================================================================*/
/*  readsd                                                                    */
/*!
    Read a buffer from a socket descriptor

    The readsd function is used to read a buffer of data from the
    server via a socket descriptor.

    @param[in]
        sd
            socket descriptor to receive data on

    @param[in]
        p
            pointer to a buffer to store the data

    @param[in]
        len
            length of data to receive

    @retval EOK - the data as received successfully
    @retval EINVAL - invalid arguments
    @retval other - error code from read()

==============================================================================*/
static int readsd( int sd, char *p, size_t len )
{
    size_t n = 0;
    size_t rcvd = 0;
    size_t remaining = len;
    int result = EINVAL;
    int count = 0;

    if ( ( p != NULL ) && ( len > 0 ) )
    {
        do
        {
            n = read( sd,
                    &p[rcvd],
                    remaining );
//            printf("SERVER: read: n = %ld\n", n);
            if ( n < 0 )
            {
                result = errno;
                if ( result != EINTR )
                {
                    break;
                }
            }
            else if ( n == 0 )
            {
                if ( ++count >= 3 )
                {
                    break;
                }
            }
            else
            {
                rcvd += n;
                remaining -= n;
            }
        }
        while ( remaining > 0 );

        if ( remaining == 0 )
        {
            result = EOK;
        }
    }

    return result;
}

/*============================================================================*/
/*  ReadPayload                                                               */
/*!
    Read the payload buffer from a socket descriptor

    The ReadPayload function checks if a payload is expected by inspecting
    the rr.len field of the request.  If this is non-zero it indicates
    the number of bytes still to be received as the message payload.
    This data is read into the client's working buffer.

    @param[in]
        sd
            socket descriptor to receive data on

    @param[in]
        pVarClient
            pointer to the VarServer client to receive the data

    @retval EOK - the data as received successfully
    @retval EINVAL - invalid arguments
    @retval E2BIG - read data will not fit into client's working buffer

==============================================================================*/
static int ReadPayload( int sd, VarClient *pVarClient )
{
    int result = EINVAL;
    size_t len;

    if ( pVarClient != NULL )
    {
        len = pVarClient->rr.len;
        pVarClient->rr.len = 0;
        if ( ( len > 0 ) && ( len <= pVarClient->workbufsize ) )
        {
//            printf("SERVER: Reading Payload: len=%ld\n", len);
            result = readsd( sd, &pVarClient->workbuf, len);
        }
        else if ( len > pVarClient->workbufsize )
        {
            result = E2BIG;
        }
        else
        {
            result = EOK;
        }
    }

    return result;
}

/*============================================================================*/
/*  InitHandlerMetrics                                                            */
/*!

    Initiialize the metrics for the request handlers

    The InitHandlerMetrics function creates a metric for each
    handler function which will track the number of invocations of
    each request handler.

    @param[in]
        pInfo
            pointer to a siginfo_t object containing the request information

    @retval EOK the request was processed successfully

==============================================================================*/
static int InitHandlerMetrics( void )
{
    int n;
    int i;
    char *pMetricName;
    int result;
    uint64_t *pMetric;

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

    result = EOK;
}

/*============================================================================*/
/*  ProcessRequest                                                            */
/*!

    Process a request from a client

    The ProcessRequest function processes requests received from clients
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
static int ProcessRequest( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    VarRequest requestType;
    int (*handler)(VarClient *pVarClient, SockRequest *pReq );
    uint64_t *pMetric;
    SockRequest req;
    ssize_t len = sizeof(SockRequest);
    ssize_t n;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        /* update the request stats */
        STATS_IncrementRequestCount();

        requestType = pReq->requestType;

        if( pVarClient->debug >= LOG_DEBUG )
        {
            printf("SERVER: Processing request %s from client %d\n",
                    RequestHandlers[requestType].requestName,
                    pVarClient->rr.clientid);
        }

/*        printf("SERVER: Processing Request\n");
        printf("SERVER:    id: %d\n", pReq->id );
        printf("SERVER:    version: %d\n", pReq->version );
        printf("SERVER:    request: %d\n", pReq->requestType );
*/
        result = EOK;

        //printf("SERVER: %s\n", RequestHandlers[requestType].requestName);
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
            result = handler( pVarClient, pReq );
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

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestOpen                                                     */
/*!
    Open a client connection

    Handle a request from a client to open its connection to the
    server.  The client open function (if specified) is called
    and the client is unblocked

    @param[in]
        pVarClient
            pointer to the variable client object

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the client was closed successfully
    @retval EINVAL invalid argument
    @retval errno error number returned by munmap

==============================================================================*/
int ProcessVarRequestOpen( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    int clientid = 0;
    ssize_t len;
    ssize_t n;
    SockResponse resp;
    VarClient *pNewVarClient = NULL;

    if( ( pVarClient != NULL ) &&
        ( pReq != NULL ) )
    {
        result = EOK;

        if( pVarClient->debug >= LOG_DEBUG )
        {
            printf("SERVER: Opening Client\n");
        }

        /* check if we need to (re)allocate a server-side working buffer */
        len = pReq->requestVal;

        if ( ( len > 0 ) && ( len != pVarClient->workbufsize ) )
        {
            /* reallocate the client to set a new work buffer size */
            pNewVarClient = realloc( pVarClient, sizeof(VarClient)+len );
            if ( pNewVarClient != NULL )
            {
                ReplaceClient( pVarClient, pNewVarClient );
                pNewVarClient->workbufsize = len + 1;
            }
            else
            {
                result = ENOMEM;
            }
        }

        /* construct client response */
        resp.id = VARSERVER_ID;
        resp.version = VARSERVER_VERSION;
        resp.requestType = pReq->requestType;
        resp.transaction_id = pReq->transaction_id;
        resp.responseVal = result;

        len = sizeof(SockResponse);
        n = write( pVarClient->sd, &resp, len );
        if ( n != len )
        {
            result = errno;
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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the client was closed successfully
    @retval EINVAL invalid argument
    @retval errno error number returned by munmap

==============================================================================*/
int ProcessVarRequestClose( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    SockResponse resp;
    ssize_t n;
    ssize_t len;

    if( pVarClient != NULL )
    {
        result = EOK;

        if( pVarClient->debug >= LOG_DEBUG )
        {
            printf("SERVER: Closing Client\n");
        }

        /* construct response message */
        resp.responseVal = EOK;
        resp.id = VARSERVER_ID;
        resp.version = VARSERVER_VERSION;
        resp.transaction_id = pReq->transaction_id;
        resp.requestType = pReq->requestType;

        len = sizeof(SockResponse);
        n = write( pVarClient->sd, &resp, len );
        if ( n != len )
        {
            result = errno;
        }
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
int ValidateClient( VarClient *pVarClient )
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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the new variable was created
    @retval ENOMEM memory allocation failure
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestNew( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    uint32_t varhandle = VAR_INVALID;
    int rc;
    VarInfo varInfo;
    ssize_t len;
    ssize_t n;
    SockResponse resp;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        len = sizeof(VarInfo);
        n = read( pVarClient->sd, &varInfo, len );
        if ( n == len )
        {
            if ( ( varInfo.var.type == VARTYPE_STR ) ||
                 ( varInfo.var.type == VARTYPE_BLOB ) )
            {
                len = pReq->requestVal;
                varInfo.var.val.blob = &pVarClient->workbuf;

                if ( ( len > 0 ) && ( len < pVarClient->workbufsize ) )
                {
                    n = read( pVarClient->sd, &pVarClient->workbuf, len );
                    if ( n == len )
                    {
                        varInfo.var.val.blob = &pVarClient->workbuf;
                    }
                }
                else
                {
                    len = varInfo.var.len;
                    if ( len <= pVarClient->workbufsize )
                    {
                        memset( &pVarClient->workbuf, 0, len );
                    }
                }
            }

            rc = VARLIST_AddNew( &varInfo, &varhandle );
        }

        resp.id = VARSERVER_ID;
        resp.version = VARSERVER_VERSION;
        resp.transaction_id = pReq->transaction_id;
        resp.responseVal = varhandle;
        resp.requestType = pReq->requestType;

        len = sizeof(SockResponse);
        n = write( pVarClient->sd, &resp, len );
        if ( n != len )
        {
            result = errno;
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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the new variable was found
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestFind( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;
    ssize_t n;
    ssize_t len;
    VarInfo varInfo;
    SockResponse resp;

    /* validate the client object */
    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        /* prepare the response */
        resp.id = VARSERVER_ID;
        resp.version = VARSERVER_VERSION;
        resp.transaction_id = pReq->transaction_id;
        resp.responseVal = VAR_INVALID;
        resp.requestType = pReq->requestType;

        len = pReq->requestVal;
        if ( len <= MAX_NAME_LEN + 1 )
        {
            varInfo.instanceID = 0;

            /* read the variable name */
            n = read( pVarClient->sd, &varInfo.name, len );
            if ( n == len )
            {
                result = VARLIST_Find( &varInfo, &resp.responseVal );
            }
        }

        /* send the response */
        len = sizeof( SockResponse );
        n = write( pVarClient->sd, &resp, len );
        if ( n != len )
        {
            result = errno;
        }
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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the request was successful
    @retval EINVAL the client is invalid

==============================================================================*/
int ProcessVarRequestEcho( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    SockResponse resp;
    ssize_t len;
    ssize_t n;

    /* validate the client object */
    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        result = EOK;

        len = sizeof(SockResponse);

        resp.id = VARSERVER_ID;
        resp.version = VARSERVER_VERSION;
        resp.transaction_id = pReq->transaction_id;
        resp.responseVal = pReq->requestVal;
        resp.requestType = pReq->requestType;

        n = write( pVarClient->sd, &resp, len );
        if ( n != len )
        {
            result = errno;
        }
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

    @param[in]
        pReq
            pointer to the received socket request

    @retval ENOTSUP the request is not supported
    @retval EINVAL the client is invalid

==============================================================================*/
int ProcessVarRequestInvalid( VarClient *pVarClient, SockRequest *pReq  )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;
    SockResponse resp;
    ssize_t len;
    ssize_t n;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        resp.id = VARSERVER_ID;
        resp.version = VARSERVER_VERSION;
        resp.transaction_id = pReq->transaction_id;
        resp.responseVal = ENOTSUP;
        resp.requestType = pReq->requestType;

        len = sizeof(SockResponse);

        n = write( pVarClient->sd, &resp, len );
        if ( n == len )
        {
            result = ENOTSUP;
        }
        else
        {
            result = errno;
        }
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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the variable info was returned
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestPrint( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    pid_t handler;
    VarInfo varInfo;
    ssize_t len;
    ssize_t n;
    PrintResponse resp;
    struct iovec iov[2];
    int vec_count = 1;

    /* validate the client object */
    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        varInfo.hVar = (VAR_HANDLE)pReq->requestVal;

        result = VARLIST_PrintByHandle( pVarClient->rr.client_pid,
                                        &varInfo,
                                        &pVarClient->workbuf,
                                        pVarClient->workbufsize,
                                        &len,
                                        pVarClient,
                                        &handler );

        resp.hVar = varInfo.hVar;
        memcpy( resp.formatspec, varInfo.formatspec, MAX_FORMATSPEC_LEN );
        memcpy( &resp.obj, &varInfo.var, sizeof( VarObject ));
        resp.responseVal = result;
        resp.len = len;

        iov[0].iov_base = &resp;
        iov[0].iov_len = sizeof( PrintResponse );
        iov[1].iov_base = NULL;
        iov[1].iov_len = 0;

        // TODO: Add EINPROGRESS (calc) and ESTRPIPE (print) handling
        if ( len > 0 )
        {
            iov[1].iov_base = &pVarClient->workbuf;
            iov[1].iov_len = len;
            vec_count = 2;
        }

        /* send the PrintResponse to the client */
        len = iov[0].iov_len + iov[1].iov_len;
        n = writev( pVarClient->sd, iov, vec_count );
        if ( n != len )
        {
            result = errno;
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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the variable info was returned
    @retval ENOENT the print session was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestOpenPrintSession( VarClient *pVarClient,
                                       SockRequest *pReq )
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
            // UnblockClient( pRequestor );

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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the print session was closed
    @retval ENOENT the print session was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestClosePrintSession( VarClient *pVarClient,
                                        SockRequest *pReq )
{
    int result = EINVAL;
    VarClient *pRequestor;
    SockResponse resp;
    size_t len;
    size_t n;

    /* validate the client object */
    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        /* get the client object of the requestor of the print */
        pRequestor = (VarClient *)TRANSACTION_Remove( pReq->requestVal );
        if( pRequestor != NULL )
        {
            result = EOK;

            resp.id = VARSERVER_ID;
            resp.version = VARSERVER_VERSION;
            resp.transaction_id = pReq->transaction_id;
            resp.responseVal = result;

            n = write( pRequestor->sd, &resp, len );
            if ( n != len )
            {
                result = errno;
            }
        }
        else
        {
            result = ENOENT;
        }

        n = write( pVarClient->sd, &resp, len );
        if ( n != len )
        {
            result = errno;
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
int ProcessVarRequestSet( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    VarInfo varInfo;
    SockResponse resp;
    ssize_t len;
    ssize_t n;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        result = EOK;
        varInfo.hVar = (VAR_HANDLE)pReq->requestVal;

        /* get the VarObject */
        len = sizeof( VarObject );
        n = read( pVarClient->sd, &varInfo.var, len );
        if ( n == len )
        {
            if ( ( varInfo.var.type == VARTYPE_STR ) ||
                 ( varInfo.var.type == VARTYPE_BLOB ) )
            {
                len = varInfo.var.len;
                if ( len < pVarClient->workbufsize )
                {
                    n = read( pVarClient->sd, &pVarClient->workbuf, len );
                    if ( n != len )
                    {
                        result = errno;
                    }
                    else
                    {
                        varInfo.var.val.blob = &pVarClient->workbuf;
                    }
                }
                else
                {
                    result = E2BIG;
                }
            }
        }
        else
        {
            result = errno;
        }

        if ( result == EOK )
        {
            result = VARLIST_Set( pVarClient->rr.client_pid,
                                &varInfo,
                                &pVarClient->validationInProgress,
                                (void *)pVarClient );

        }

        resp.id = VARSERVER_ID;
        resp.version = VARSERVER_VERSION;
        resp.requestType = pReq->requestType;
        resp.transaction_id = pReq->transaction_id;
        resp.responseVal = result;

        len = sizeof(SockResponse);
        n = write( pVarClient->sd, &resp, len );
        if ( n != len )
        {
            result = errno;
        }
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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the variable type was retrieved
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestType( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    SockResponse resp;
    VarInfo varInfo;
    ssize_t n;
    ssize_t len;

    /* validate the client object */
    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        varInfo.hVar = (VAR_HANDLE)pReq->requestVal;

        result = VARLIST_GetType( &varInfo );

        resp.id = VARSERVER_ID;
        resp.version = VARSERVER_VERSION;
        resp.requestType = pReq->requestType;
        resp.transaction_id = pReq->transaction_id;
        resp.responseVal = result == EOK ? varInfo.var.type : VARTYPE_INVALID;

        len = sizeof( SockResponse );
        n = write( pVarClient->sd, &resp, len );
        if ( n != len )
        {
            result = errno;
        }
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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the variable type was retrieved
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestName( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    VarInfo varInfo;
    ssize_t len;
    ssize_t n;
    struct iovec iov[2];
    SockResponse resp;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        varInfo.hVar = (VAR_HANDLE)pReq->requestVal;

        /* fetch the variable name */
        result = VARLIST_GetName( &varInfo );

        resp.id = VARSERVER_ID;
        resp.version = VARSERVER_VERSION;
        resp.requestType = pReq->requestType;
        resp.transaction_id = pReq->transaction_id;
        resp.responseVal = result;

        iov[0].iov_base = &resp;
        iov[0].iov_len = sizeof(SockResponse);
        iov[1].iov_base = varInfo.name;
        iov[1].iov_len = MAX_NAME_LEN+1;

        len = iov[0].iov_len + iov[1].iov_len;
        n = writev( pVarClient->sd, iov, 2 );
        if ( n != len )
        {
            result = errno;
        }
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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the variable length was retrieved
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestLength( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    SockResponse resp;
    VarInfo varInfo;
    ssize_t n;
    ssize_t len;

    /* validate the client object */
    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        varInfo.hVar = (VAR_HANDLE)pReq->requestVal;

        result = VARLIST_GetLength( &varInfo );

        resp.id = VARSERVER_ID;
        resp.version = VARSERVER_VERSION;
        resp.requestType = pReq->requestType;
        resp.transaction_id = pReq->transaction_id;
        resp.responseVal = result == EOK ? varInfo.var.len : -1;

        len = sizeof( SockResponse );
        n = write( pVarClient->sd, &resp, len );
        if ( n != len )
        {
            result = errno;
        }
    }

    return result;}

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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the variable notification was successfully registered
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestNotify( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    SockResponse resp;
    ssize_t len;
    ssize_t n;
    VarInfo varInfo;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        varInfo.hVar = pReq->requestVal;
        varInfo.notificationType = pReq->requestVal2;

        /* register the notification request */
        result = VARLIST_RequestNotify( &varInfo,
                                        pVarClient->rr.client_pid );

        resp.id = VARSERVER_ID;
        resp.version = VARSERVER_VERSION;
        resp.transaction_id = pReq->transaction_id;
        resp.responseVal = result;

        n = write( pVarClient->sd, &resp, len );
        if ( n != len )
        {
            result = errno;
        }
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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the variable info was returned
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestGet( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    SockResponse resp;
    ssize_t len;
    ssize_t n;
    struct iovec iov[3];
    VarInfo varInfo;
    int vec_count = 2;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        varInfo.hVar = (VAR_HANDLE)pReq->requestVal;

        iov[0].iov_base = &resp;
        iov[0].iov_len = sizeof(SockResponse);
        iov[1].iov_base = &varInfo.var;
        iov[1].iov_len = sizeof(VarObject);
        iov[2].iov_base = NULL;
        iov[2].iov_len = 0;

        len = 0;
        result = VARLIST_GetByHandle( pVarClient->rr.client_pid,
                                      &varInfo,
                                      &pVarClient->workbuf,
                                      pVarClient->workbufsize,
                                      &len );

        if ( len != 0 )
        {
            iov[2].iov_base = &pVarClient->workbuf;
            iov[2].iov_len = len;
            vec_count = 3;
        }

        len = iov[0].iov_len + iov[1].iov_len + iov[2].iov_len;
        n = writev( pVarClient->sd, iov, 3 );
        if ( n != len )
        {
            result = errno;
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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the variable info was returned
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestGetFirst( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;
    SockResponse resp;
    ssize_t len;
    ssize_t n;
    struct iovec iov[2];
    VarInfo varInfo;
    VarQuery query;
    int context = -1;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        /* read the variable query */
        len = sizeof( VarQuery );
        n = read( pVarClient->sd, &query, len );
        if ( n == len )
        {
            /* populate the VarInfo object used in the query */
            varInfo.flags = query.flags;
            memcpy(&varInfo.tagspec, &query.tagspec, MAX_TAGSPEC_LEN );

            /* populate the query match string in the client's working buffer */
            if ( query.match[0] != 0 )
            {
                len = strlen( query.match );
                if ( len < pVarClient->workbufsize )
                {
                    strcpy( &pVarClient->workbuf, query.match );
                }
            }

            /* start the query */
            len = 0;
            result = VARLIST_GetFirst( pVarClient->rr.client_pid,
                                       query.type,
                                       &varInfo,
                                       &pVarClient->workbuf,
                                       pVarClient->workbufsize,
                                       &len,
                                       &context );

            if( result == EINPROGRESS )
            {
                /* add the client to the blocked clients list */
                BlockClient( pVarClient, NOTIFY_CALC );
            }
            else
            {
                /* build the response */
                resp.id = VARSERVER_ID;
                resp.version = VARSERVER_VERSION;
                resp.transaction_id = pReq->transaction_id;
                resp.requestType = pReq->requestType;
                resp.responseVal = context;

                iov[0].iov_base = &resp;
                iov[0].iov_len = sizeof(SockResponse);
                iov[1].iov_base = &varInfo;
                iov[1].iov_len = sizeof(VarInfo);

                /* calculate the length of the response */
                len = iov[0].iov_len + iov[1].iov_len;

                /* send the response */
                n = writev( pVarClient->sd, iov, 2 );
                if ( n != len )
                {
                    result = errno;
                }
            }
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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the variable info was returned
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestGetNext( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;
    int context = -1;
    VarInfo varInfo;
    struct iovec iov[2];
    SockResponse resp;
    ssize_t len;
    ssize_t n;
    char *p;
    size_t i;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        context = pReq->requestVal;

        len = 0;
        memset(&varInfo, 0, sizeof(VarInfo));

        result = VARLIST_GetNext( pVarClient->rr.client_pid,
                                  pReq->requestVal,
                                  &varInfo,
                                  &pVarClient->workbuf,
                                  pVarClient->workbufsize,
                                  &len,
                                  &context );
        if( result == EINPROGRESS )
        {
            /* add the client to the blocked clients list */
            BlockClient( pVarClient, NOTIFY_CALC );
        }
        else
        {
            /* build the response */
            resp.id = VARSERVER_ID;
            resp.version = VARSERVER_VERSION;
            resp.transaction_id = pReq->transaction_id;
            resp.requestType = pReq->requestType;
            resp.responseVal = context;

            iov[0].iov_base = &resp;
            iov[0].iov_len = sizeof(SockResponse);
            iov[1].iov_base = &varInfo;
            iov[1].iov_len = sizeof(VarInfo);

            /* calculate the length of the response */
            len = iov[0].iov_len + iov[1].iov_len;

            /* send the response */
            n = writev( pVarClient->sd, iov, 2 );
            if ( n != len )
            {
                result = errno;
            }
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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the validation information was returned
    @retval ENOENT the validation request was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessValidationRequest( VarClient *pVarClient, SockRequest *pReq )
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

    @param[in]
        pReq
            pointer to the received socket request

    @retval EOK the validation information was returned
    @retval ENOENT the validation request was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessValidationResponse( VarClient *pVarClient, SockRequest *pReq )
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
                result = ProcessVarRequestSet( pSetClient, pReq );
            }

            /* unblock the client */
            // UnblockClient( pSetClient );
        }
    }

    return result;

}

/*============================================================================*/
/*  AddRenderHandler                                                          */
/*!
    Add a new render handler

    The AddRenderHandler function adds a render handling function to
    the list of render handling functions.   Each render handling function
    is associated with a variable handle, and when a request is received
    to print that variable, the render handling function will be invoked.

    @param[in]
        hVar
            Variable handle associated with the render handling function

    @param[in]
        fn
            pointer to the render handling function

    @retval EOK the render handling function was registered successfully
    @retval ENOMEM memory allocation failure
    @retval EINVAL invalid arguments

==============================================================================*/
int AddRenderHandler( VAR_HANDLE hVar,
                      int (*fn)(VarInfo *pVarInfo, char *buf, size_t len) )
{
    int result = EINVAL;
    RenderHandler *pRenderHandler;

    if ( ( hVar != VAR_INVALID ) &&
         ( fn != NULL ) )
    {
        /* allocate memory for the RenderHandler */
        pRenderHandler = calloc( 1, sizeof( RenderHandler ) );
        if ( pRenderHandler != NULL )
        {
            /* populate the node */
            pRenderHandler->hVar = hVar;
            pRenderHandler->fn = fn;

            /* insert the node into the start of the render handlers list */
            pRenderHandler->pNext = pRenderHandlers;
            pRenderHandlers = pRenderHandler;

            result = EOK;
        }
        else
        {
            result = ENOMEM;
        }
    }

    return result;
}

/*! @}
 * end of sockserver group */
