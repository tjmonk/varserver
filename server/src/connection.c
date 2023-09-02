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
 * @defgroup connection Connection Manager
 * @brief Manages a list of socket connections
 * @{
 */

/*============================================================================*/
/*!
@file connection.c

    Connection Manager

    The Connection Manager manages a list of socket connections
    to the variable server.

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
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include <varserver/varclient.h>
#include <varserver/sockapi.h>
#include "clientlist.h"
#include "connection.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*==============================================================================
        Private types
==============================================================================*/

/*! The connection type defines the type of connection */
typedef enum _connectionType
{
    /*! connection not yet determined */
    CONNTYPE_UNKNOWN = 0,

    /*! client connection */
    CONNTYPE_CLIENT = 1,

    /*! notification channel */
    CONNTYPE_NOTIFY = 2,

    /*! stream for rendering */
    CONNTYPE_STREAM = 3

} ConnectionType;

/*! The Connection object is used to manage socket connections to
    the variable server */
typedef struct _connection
{
    /*! connnection socket descriptor */
    int sd;

    /*! pointer to the VarClient */
    VarClient *pVarClient;

    /*! connection type */
    ConnectionType type;

    /*! peer connection socket descriptor for streams */
    int peer_sd;

    /*! used for chaining connections in a list */
    struct _connection *pNext;
} Connection;

/*==============================================================================
        Private function declarations
==============================================================================*/

static int ConnectionNew( int sd );

static int ConnectionGetCount(void);

static int ConnectionGetfds( int max_sd, fd_set *pfds );

static int ConnectionCheckNew( int sock, fd_set *pfds );

static int ConnectionRxRequest( Connection *pConnection,
                                int (*fn)( VarClient *pVarClient,
                                           SockRequest *pReq ) );

static int ConnectionHandleReq( Connection *pConnection,
                                SockRequest *pReq,
                                int (*fn)( VarClient *pVarClient,
                                           SockRequest *pReq ) );

static int ConnectionRxStream( Connection *pConnection );

static int ConnectionClose( Connection *pConnection );

static int ConnectionRx( int sock,
                         fd_set *pfds,
                         int (*fn)( VarClient *pVarClient, SockRequest *pReq ));

static int ConnectionSetNotificationSocket( Connection *pConnection,
                                            int client_id );

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! active connection list */
static Connection *connectionlist = NULL;

/*! list of available VarClient objects */
static Connection *freelist = NULL;

/*! number of active and inactive clients */
static int NumConnections = 0;

/*==============================================================================
        Public function definitions
==============================================================================*/

/*============================================================================*/
/*  ConnectionProcessor                                                       */
/*!
    Run the connection processor

    The ConnectionProcessor handles new connections, and processes data
    for existing connections.

    @param[in]
        sock
            socket interface to listen for new connections

    @param[in]
        fn
            pointer to the request processing functions

    @retval EOK a transaction was successfully processed
    @retval EINVAL invalid arguments
    @retval EINTR interrupted by signal
    @retval other error from select

==============================================================================*/
int ConnectionProcessor( int sock,
                         int (*fn)( VarClient *pVarClient, SockRequest *pReq ) )
{
    fd_set readfds;
    static fd_set savefds;
    static int connectionCount = -1;
    int max_sd;
    int activity;
    int numConnections;
    int result = EINVAL;

    if ( ( sock != -1 ) &&
         ( fn != NULL ) )
    {
        if ( connectionCount == -1 )
        {
            /* one time initialization */
            FD_ZERO( &savefds );
            FD_SET( sock, &savefds );
            max_sd = sock;
        }

        readfds = savefds;
        numConnections = ConnectionGetCount();
        if ( numConnections != connectionCount )
        {
            FD_ZERO( &readfds );
            FD_SET( sock, &readfds );
            max_sd = ConnectionGetfds( sock, &readfds );
            savefds = readfds;
            connectionCount = numConnections;
        }

        /* wait for an activity on one of the sockets */
        activity = select( max_sd + 1 , &readfds , NULL , NULL , NULL);
        if ( activity > 0 )
        {
            /* handle new client connections */
            ConnectionCheckNew( sock, &readfds );

            /* handle client requests */
            ConnectionRx( sock, &readfds, fn );

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

/*============================================================================*/
/*  ConnectionNew                                                             */
/*!
    Create a new Connection

    The NewConnection function creates a new Connection object

    @param[in]
        sd
            client socket descriptor

    @retval EOK New connection created ok
    @retval ENOMEM memory allocation failure

==============================================================================*/
static int ConnectionNew( int sd )
{
    Connection *pConnection = NULL;

    if ( freelist != NULL )
    {
        /* get the connection from the free list */
        pConnection = freelist;
        freelist = pConnection->pNext;
    }
    else
    {
        /* allocate a new Connection */
        pConnection = calloc( 1, sizeof( Connection ) );
    }

    if ( pConnection != NULL )
    {
        /* set the connection socket descriptor */
        pConnection->sd = sd;

        /* increment the number of connections */
        NumConnections++;

        /* add the Connection to the connection list */
        pConnection->pNext = connectionlist;
        connectionlist = pConnection;
    }

    return pConnection != NULL ? EOK : ENOMEM;
}

/*============================================================================*/
/*  ConnectionGetCount                                                        */
/*!
    Get the number of connections

    The ConnectionGetCount function returns the number of active connections
    to the Variable Server

    @retval number of active connections

==============================================================================*/
static int ConnectionGetCount(void)
{
    return NumConnections;
}

/*============================================================================*/
/*  ConnectionGetfds                                                          */
/*!
    Get connections fd_set

    The ConnectionGetfds function gets the FDSET for the connections
    to the variable server.

    @param[in]
        max_sd
            the maximum socket descriptor in the specified fd_set

    @param[in,out]
        pfds
            pointer to an fd_set of socket descriptors to be updated

    @retval maximum socket descriptor in the updated fd_set

==============================================================================*/
static int ConnectionGetfds( int max_sd, fd_set *pfds )
{
    int sd;
    Connection *pConnection = connectionlist;

    /* convert the client socket descriptor map to an fd_set */
    if ( pfds != NULL )
    {
        while( pConnection != NULL )
        {
            if ( pConnection->type != CONNTYPE_NOTIFY )
            {
                sd = pConnection->sd;

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
            pConnection = pConnection->pNext;
        }
    }

    return max_sd;
}

/*============================================================================*/
/*  ConnectionCheckNew                                                        */
/*!
    Check for an incoming connection

    The ConnectionCheckNew function checks for a new connection
    on the listening socket

    @param[in]
        sock
            the listening socket which is accepting new connections

    @param[in,out]
        pfds
            pointer to an fd_set of socket descriptors to be updated

    @retval EINVAL invalid arguments
    @retval ENOMEM memory allocation failure
    @retval ENOENT no new connection
    @retval EBADF bad socket descriptor
    @retval EOK new connection accepted

==============================================================================*/
static int ConnectionCheckNew( int sock, fd_set *pfds )
{
    int addrlen = sizeof( struct sockaddr_in );
    struct sockaddr_in address;
    int sd;
    int result = EINVAL;

    if ( ( sock != -1 ) && ( pfds != NULL ) )
    {
        result = ENOENT;

        /* If something happened on the listening socket then it is an
           incoming connection */
        if ( FD_ISSET( sock, pfds ) )
        {
            sd = accept( sock,
                         (struct sockaddr *)&address,
                         (socklen_t *)&addrlen );
            if ( sd > 0 )
            {
                /* add the new connection */
                result = ConnectionNew( sd );
                if ( result != EOK )
                {
                    printf("Connection denied: %d\n", sd );
                    close( sd );
                }
                else
                {
                    /* report new connection */
                    printf( "New connection: "
                            "fd : %d, "
                            "ip : %s, "
                            "port : %d\n",
                            sd,
                            inet_ntoa(address.sin_addr),
                            ntohs(address.sin_port));
                }
            }
            else
            {
                printf("accept: %s\n", strerror(errno));
                result = EBADF;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  ConnectionRx                                                              */
/*!
    Handle received data

    The ConnectionRx function handles data received on the monitored
    connections.  Depending on the connection type, the data is handled
    differently.


    @param[in]
        sock
            the listening socket which is accepting new connections

    @param[in,out]
        pfds
            pointer to an fd_set of socket descriptors to be checked

    @param[in]
        fn
            pointer to the data processing function

    @retval EINVAL invalid arguments
    @retval EOK no errors

==============================================================================*/
static int ConnectionRx( int sock,
                         fd_set *pfds,
                         int (*fn)( VarClient *pVarClient, SockRequest *pReq ) )
{
    int result = EINVAL;
    int sd;
    Connection *pConnection;
    Connection *p = connectionlist;

    if ( pfds != NULL )
    {
        result = EOK;

        while( p != NULL )
        {
            /* get a pointer to the connection to be processed */
            pConnection = p;

            /* get the socket descriptor */
            sd = pConnection->sd;

            /* get the next connection */
            p = p->pNext;

            if ( sd > 0 )
            {
                if ( FD_ISSET( sd, pfds ) )
                {
                    switch( pConnection->type )
                    {
                        /*! connection not yet determined */
                        case CONNTYPE_UNKNOWN:
                            ConnectionRxRequest( pConnection, fn );
                            break;

                        case CONNTYPE_CLIENT:
                            ConnectionRxRequest( pConnection, fn );
                            break;

                        case CONNTYPE_NOTIFY:
                            /* should not receive data on notify channel */
                            printf("Rx on Notify channel %d!\n", sd);
                            //ConnectionClose( pConnection );
                            break;

                        /*! stream for rendering */
                        case CONNTYPE_STREAM:
                            ConnectionRxStream( pConnection );
                            break;

                        default:
                            break;
                    }
                }
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  ConnectionRxRequest                                                       */
/*!
    Receive and Handle a request

    The ConnectionRxRequest function reads and handles a request received from
    a client.

    @param[in]
        pConnection
            pointer to the connection the request arrived on

    @param[in]
        fn
            pointer to the data processing function to handle the request

    @retval EINVAL invalid arguments
    @retval EOK no errors

==============================================================================*/
static int ConnectionRxRequest( Connection *pConnection,
                                int (*fn)( VarClient *pVarClient,
                                           SockRequest *pReq ) )
{
    int result = EINVAL;
    struct sockaddr_in address;
    int addrlen;
    ssize_t n;
    SockRequest req;
    VarClient *pVarClient;
    int sd;

    if ( ( pConnection != NULL ) &&
         ( fn != NULL ) )
    {
        sd = pConnection->sd;

        /* read incoming message */
        n = read( sd, &req, sizeof(SockRequest) );
        if ( n <= 0 )
        {
            pVarClient = pConnection->pVarClient;

            /* client disconnected */
            getpeername( sd,
                        (struct sockaddr *)&address,
                        (socklen_t *)&addrlen );

            printf( "Client disconnected: "
                    "id: %d, "
                    "ip: %s, "
                    "port: %d\n",
                    pVarClient != NULL ? pVarClient->rr.clientid : -1,
                    inet_ntoa(address.sin_addr),
                    ntohs(address.sin_port) );

            ConnectionClose( pConnection );
        }
        else if ( n != sizeof(SockRequest) )
        {
            printf("SERVER: Invalid read: n=%ld\n", n );
        }
        else if ( ( req.id == VARSERVER_ID ) &&
                  ( req.version == VARSERVER_VERSION ) )
        {
            ConnectionHandleReq( pConnection, &req, fn );
        }
        else
        {
            printf("SERVER: Invalid read\n");
        }
    }

    return result;
}

/*============================================================================*/
/*  ConnectionHandleReq                                                       */
/*!
    Handle received request

    The ConnectionRxRequest function handles a request received from
    a client.

    @param[in]
        pConnection
            pointer to the connection the request arrived on

    @param[in]
        pReq
            pointer to the received request

    @param[in]
        fn
            pointer to the data processing function to handle the request

    @retval EINVAL invalid arguments
    @retval ENOMEM memory allocation failure while handling the request
    @retval ENOENT no handler for the request
    @retval ENOTSUP operation not supported
    @retval EOK no errors

==============================================================================*/
static int ConnectionHandleReq( Connection *pConnection,
                                SockRequest *pReq,
                                int (*fn)( VarClient *pVarClient,
                                           SockRequest *pReq ) )
{
    int result = EINVAL;
    Connection *p = connectionlist;

    if ( ( pConnection != NULL ) &&
         ( pReq != NULL ) &&
         ( fn != NULL ) )
    {
        switch ( pReq->requestType )
        {
            case VARREQUEST_OPEN:
                pConnection->type = CONNTYPE_CLIENT;
                pConnection->pVarClient = NewClient( pConnection->sd,
                                                     pReq->requestVal );
                if ( pConnection->pVarClient != NULL )
                {
                    result = fn( pConnection->pVarClient, pReq );
                }
                else
                {
                    ConnectionClose( pConnection );
                    result = ENOMEM;
                }
                break;

            case VARREQUEST_NOTIFY:
                if ( pConnection->pVarClient != NULL )
                {
                    result = fn( pConnection->pVarClient, pReq );
                }
                else
                {
                    /* set up the notification socket descriptor */
                    result = ConnectionSetNotificationSocket( pConnection,
                                                              pReq->requestVal);
                }
                break;

            default:
                /* no special connection handling for other requests */
                if ( pConnection->pVarClient != NULL )
                {
                    result = fn( pConnection->pVarClient, pReq );
                }
                else
                {
                    ConnectionClose( pConnection );
                    result = ENOTSUP;
                }
                break;
        }
    }

    return result;
}

/*============================================================================*/
/*  ConnectionSetNotificationSocket                                           */
/*!
    Associate a notification socket with a client

    The ConnectionSetNotificationSocket function associates a var client
    with a socket to be used for notification requests.

    @param[in]
        pConnection
            pointer to the connection associated with the notification socket

    @param[in]
        client_id
            client identiifer of the client to associate with the
            socket descriptor


    @retval EINVAL invalid arguments
    @retval ENOENT client not found
    @retval EOK no errors

==============================================================================*/
static int ConnectionSetNotificationSocket( Connection *pConnection,
                                            int client_id )
{
    int result = EINVAL;
    VarClient *pVarClient;
    SockResponse resp;
    ssize_t len;
    ssize_t n;

    if ( pConnection != NULL )
    {
        pConnection->type = CONNTYPE_NOTIFY;
        pVarClient = GetClientByID( client_id );
        if ( pVarClient != NULL )
        {
            pVarClient->notify_sd = pConnection->sd;
            pConnection->pVarClient = pVarClient;

            len = sizeof(SockResponse);
            resp.id = VARSERVER_ID;
            resp.version = VARSERVER_VERSION;
            resp.responseVal = EOK;
            resp.requestType = VARREQUEST_NOTIFY;
            resp.responseVal2 = 0;
            resp.transaction_id = 0;
            n = write( pVarClient->notify_sd, &resp, len );
            if ( n == len )
            {
                result = EOK;
            }
            else
            {
                result = errno;
            }
        }
        else
        {
            ConnectionClose( pConnection );
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  ConnectionRxStream                                                        */
/*!
    Receive Stream data

    The ConnectionRxStream function reads and dispatches stream data
    to the appropriate socket descriptor.

    @param[in]
        pConnection
            pointer to the connection the request arrived on

    @retval EINVAL invalid arguments
    @retval EBADF invalid socket descriptor
    @retval EOK no errors

==============================================================================*/
static int ConnectionRxStream( Connection *pConnection )
{
    char buf[BUFSIZ];
    ssize_t n;
    int sd;
    int peer_sd;
    int result = EINVAL;

    if ( pConnection != NULL )
    {
        sd = pConnection->sd;
        peer_sd = pConnection->peer_sd;

        if ( ( sd > 0 ) && ( peer_sd > 0 ) )
        {
            n = read( sd, buf, BUFSIZ );
            if ( n > 0 )
            {
                write( peer_sd, buf, n );
            }
            else
            {
                ConnectionClose( pConnection );
            }

            result = EOK;
        }
        else
        {
            result = EBADF;
        }
    }

    return result;
}

/*============================================================================*/
/*  ConnectionClose                                                           */
/*!
    Close the connection

    The ConnectionClose function closes the specified connection, closes
    an associated socket descriptors, deletes any associated client,
    and moves the connection to the free list.

    @param[in]
        pConnection
            pointer to the connection to be closed

    @retval EINVAL invalid arguments
    @retval EOK no errors

==============================================================================*/
static int ConnectionClose( Connection *pConnection )
{
    int result = EINVAL;
    Connection *p = connectionlist;

    if ( pConnection != NULL )
    {
        result = EOK;

        if ( pConnection->sd )
        {
            close( pConnection->sd );
            pConnection->sd = -1;
        }

        if ( pConnection->peer_sd )
        {
            close( pConnection->peer_sd );
            pConnection->peer_sd = -1;
        }

        if ( pConnection->pVarClient != NULL )
        {
            DeleteClient( pConnection->pVarClient );
            pConnection->pVarClient = NULL;
        }

        /* remove the connection from the connectioni list */
        if ( pConnection == connectionlist )
        {
            connectionlist = connectionlist->pNext;
        }
        else
        {
            while ( p != NULL )
            {
                if ( p->pNext == pConnection )
                {
                    p->pNext = pConnection->pNext;
                    break;
                }

                p = p->pNext;
            }
        }

        /* put connection back on the free list */
        pConnection->type = CONNTYPE_UNKNOWN;
        pConnection->pNext = freelist;
        freelist = pConnection;

        NumConnections--;
    }

    return result;
}

/*============================================================================*/
/*  ConnectionSendVar                                                         */
/*!
    Send a Var object to a client connection

    The ConnectionSendVar function sends a Var object to a client connection

    @param[in]
        pVarClient
            Pointer to the client data structure

    @param[in]
        pVarInfo
            pointer to the VarInfo object containing the Var object to send

    @retval EOK the variable object was sent
    @retval EINVAL invalid arguments

==============================================================================*/
int ConnectionSendVar( VarClient *pVarClient, VarInfo *pVarInfo )
{
    int result = EINVAL;
    SockResponse resp;
    ssize_t len;
    ssize_t n;
    struct iovec iov[3];
    VarInfo varInfo;
    int vec_count = 2;
    VarType type;

    if ( ( pVarClient != NULL ) &&
         ( pVarInfo != NULL ) )
    {
        printf("Unblocking client: %d\n", pVarClient->rr.clientid);
        iov[0].iov_base = &resp;
        iov[0].iov_len = sizeof(SockResponse);
        iov[1].iov_base = &(pVarInfo->var);
        iov[1].iov_len = sizeof(VarObject);
        iov[2].iov_base = NULL;
        iov[2].iov_len = 0;

        type = pVarInfo->var.type;
        switch( type )
        {
            case VARTYPE_BLOB:
                len = pVarInfo->var.len;
                break;

            case VARTYPE_STR:
                len = strlen( pVarInfo->var.val.str ) + 1;
                break;

            default:
                len = 0;
        }

        if ( len > 0 )
        {
            iov[2].iov_base = pVarInfo->var.val.blob;
            iov[2].iov_len = len;
            vec_count = 3;
        }

        len = iov[0].iov_len + iov[1].iov_len + iov[2].iov_len;
        n = writev( pVarClient->sd, iov, 3 );
        if ( n == len )
        {
            result = EOK;
        }
        else
        {
            result = errno;
        }
    }

    return result;
}

/*! @}
 * end of connection group */
