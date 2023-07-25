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
#include "varlist.h"
#include "taglist.h"
#include "blocklist.h"
#include "transaction.h"
#include "stats.h"
#include "clientlist.h"
#include "server.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*! varserver listening port (VS = 0x5653) */
#define PORT 22099

/*==============================================================================
        Private types
==============================================================================*/

/*==============================================================================
        Private function declarations
==============================================================================*/

static int SetupListener( void );
static int HandleNewClient( int sock, fd_set *pfds );
static int HandleClientRequest( int sock, fd_set *pfds );
static int SendClientResponse( VarClient *pVarClient );
static int writesd( int sd, char *p, size_t len );

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*==============================================================================
        Public function definitions
==============================================================================*/

/*============================================================================*/
/*  SOCSERVER_Run                                                             */
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
                    n = read( sd, buffer, 1024 );
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

                        DeleteClient( pVarClient );
                    }
                    else
                    {
                        /* echo back to the client what it sent */
                        buffer[n] = '\0';
                        send(sd, buffer, strlen(buffer), 0 );
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

        /* get the socket descriptor */
        sd = pVarClient->sd;

        /* send response */
        result = writesd( sd,
                          (char *)&pVarClient->rr,
                          sizeof( RequestResponse ) );
        if ( rc == EOK )
        {
            /* check the length of the (optional) request response body */
            len = pVarClient->rr.len;
            if ( len > 0 )
            {
                /* write data from the working buffer */
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

/*! @}
 * end of sockserver group */
