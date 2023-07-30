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
#include "handlers.h"
#include "sockserver.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*! varserver listening port (VS = 0x5653) */
#define PORT 22099

/*==============================================================================
        Private types
==============================================================================*/

typedef struct _RequestReceiver
{
    VarRequest requestType;

    int (*handler)( VarClient *pVarClient, SockRequest *pReq );
} RequestReceiver;

/*==============================================================================
        Private function declarations
==============================================================================*/

static int SetupListener( void );
static int SetupClient( VarClient *pVarClient );
static int HandleNewClient( int sock, fd_set *pfds );
static int HandleClientRequest( int sock, fd_set *pfds );
static int SendClientResponse( VarClient *pVarClient );
static int ReadPayload( int sd, VarClient *pVarClient );

static int writesd( int sd, char *p, size_t len );
static int readsd( int sd, char *p, size_t len );

static int RxRequest( VarClient *pVarClient, SockRequest *pReq );
static int RxOpen( VarClient *pVarClient, SockRequest *pReq );
static int RxClose( VarClient *pVarClient, SockRequest *pReq );
static int RxEcho( VarClient *pVarClient, SockRequest *pReq );
static int RxNew( VarClient *pVarClient, SockRequest *pReq );
static int RxFind( VarClient *pVarClient, SockRequest *pReq );
static int RxGet( VarClient *pVarClient, SockRequest *pReq );
static int RxPrint( VarClient *pVarClient, SockRequest *pReq );
static int RxSet( VarClient *pVarClient, SockRequest *pReq );
static int RxType( VarClient *pVarClient, SockRequest *pReq );
static int RxName( VarClient *pVarClient, SockRequest *pReq );
static int RxLength( VarClient *pVarClient, SockRequest *pReq );
static int RxNotify( VarClient *pVarClient, SockRequest *pReq );
static int RxGetValidationRequest( VarClient *pVarClient, SockRequest *pReq );
static int RxSendValidationResponse( VarClient *pVarClient, SockRequest *pReq );
static int RxOpenPrintSession( VarClient *pVarClient, SockRequest *pReq );
static int RxClosePrintSession( VarClient *pVarClient, SockRequest *pReq );
static int RxGetFirst( VarClient *pVarClient, SockRequest *pReq );
static int RxGetNext( VarClient *pVarClient, SockRequest *pReq );

/*==============================================================================
        Private file scoped variables
==============================================================================*/

static RequestReceiver RequestReceivers[] =
{
    { VARREQUEST_INVALID, NULL },
    { VARREQUEST_OPEN, RxOpen },
    { VARREQUEST_CLOSE, RxClose },
    { VARREQUEST_ECHO, RxEcho },
    { VARREQUEST_NEW, RxNew },
    { VARREQUEST_FIND, RxFind },
    { VARREQUEST_GET, RxGet },
    { VARREQUEST_PRINT, RxPrint },
    { VARREQUEST_SET, RxSet },
    { VARREQUEST_TYPE, RxType },
    { VARREQUEST_NAME, RxName },
    { VARREQUEST_LENGTH, RxLength },
    { VARREQUEST_NOTIFY, RxNotify },
    { VARREQUEST_GET_VALIDATION_REQUEST, RxGetValidationRequest },
    { VARREQUEST_SEND_VALIDATION_RESPONSE, RxSendValidationResponse },
    { VARREQUEST_OPEN_PRINT_SESSION, RxOpenPrintSession },
    { VARREQUEST_CLOSE_PRINT_SESSION, RxClosePrintSession },
    { VARREQUEST_GET_FIRST, RxGetFirst },
    { VARREQUEST_GET_NEXT, RxGetNext }
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
                    pVarClient->pFnUnblock = SendClientResponse;
                    pVarClient->pFnOpen = SetupClient;
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
                    else if ( n != sizeof(RequestResponse) )
                    {
                        printf("SERVER: Invalid read: n=%ld\n", n );
                    }
                    else if ( ( req.id == VARSERVER_ID ) &&
                              ( req.version == VARSERVER_VERSION ) )
                    {
                        RxRequest( pVarClient, &req );

//                        printf("SERVER: Handling Request\n");
//                        printf("SERVER:    id: %d\n", pVarClient->rr.id );
//                        printf("SERVER:    version: %d\n", pVarClient->rr.version );
//                        printf("SERVER:    request: %d\n", pVarClient->rr.requestType );

                        HandleRequest( pVarClient );
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

static int RxRequest( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;
    int (*handler)( VarClient *, SockRequest *);
    if ( ( pVarClient != NULL ) && ( pReq != NULL ) )
    {
        if ( pReq->requestType < VARREQUEST_END_MARKER )
        {
            pVarClient->rr.requestVal = pReq->requestVal;

            handler = RequestReceivers[pReq->requestType].handler;
            if ( handler != NULL )
            {
                /* execute the handler to receive the data */
                result = handler( pVarClient, pReq );
            }
        }
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
/*  SetupClient                                                               */
/*!
    Set up the client

    The SetupClient function is invoked as part of the client creation.
    It allocates the server-side working buffer based on the length
    specified in the RequestResponse object.

    @param[in]
        pVarClient
            pointer to the VarClient object belonging to the client

    @retval EOK - the client setup was successful
    @retval EINVAL - invalid arguments
    @retval ENOMEM - memory allocation failure

==============================================================================*/
static int SetupClient( VarClient *pVarClient )
{
    int result;
    size_t len;
    int idx;
    VarClient *pNewVarClient;

    if ( pVarClient != NULL )
    {
        result = EOK;

        /* check if we need to (re)allocate a server-side working buffer */
        len = pVarClient->rr.len;
        pVarClient->rr.len = 0;

        if ( ( len > 0 ) && ( len != pVarClient->workbufsize ) )
        {
            /* reallocate the client to set a new work buffer size */
            pNewVarClient = realloc( pVarClient, sizeof(VarClient)+len );
            if ( pNewVarClient != NULL )
            {
                ReplaceClient( pVarClient, pNewVarClient );
                pNewVarClient->workbufsize = len + 1;

                /* this replaces the unblock client call which we
                cannot use since we just invalidated the pVarClient pointer */
                SendClientResponse( pNewVarClient );
            }
            else
            {
                result = ENOMEM;
            }
        }
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

static int RxOpen( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {
        pVarClient->rr.requestVal = pReq->requestVal;
    }

    return result;
}

static int RxClose( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxEcho( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxNew( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxFind( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxGet( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxPrint( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxSet( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxType( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxName( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxLength( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxNotify( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxGetValidationRequest( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxSendValidationResponse( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxOpenPrintSession( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxClosePrintSession( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxGetFirst( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}

static int RxGetNext( VarClient *pVarClient, SockRequest *pReq )
{
    int result = EINVAL;

    if ( ( pVarClient != NULL ) &&
         ( pReq != NULL ) )
    {

    }

    return result;

}


/*! @}
 * end of sockserver group */
