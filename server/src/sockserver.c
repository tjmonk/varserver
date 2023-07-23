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

/*==============================================================================
        Private file scoped variables
==============================================================================*/

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
int main(int argc, char *argv[])
{
    int sock;
    fd_set readfds;
    fd_set savefds;
    int max_sd;
    int activity;
    int clientcount = 0;
    int numclients;

    /* setup listener to accept client connections */
    sock = SetupListener();
    if ( sock != -1 )
    {
        /* setup file descriptor to wait on */
        FD_ZERO( &savefds );
        FD_SET( sock, &savefds );
        max_sd = sock;

        while (1)
        {
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
            }
            else if ( ( activity < 0 ) && ( errno!=EINTR ) )
            {
                printf("select error: %s\n", strerror(errno));
                exit(1);
            }
        }
    }

    return 0;
}

static int SetupListener( void )
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
    char *message = "ECHO Daemon v1.0 \r\n";
    int result = EINVAL;
    VarClient *pVarClient;
    int clientid;

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
                clientid = ( pVarClient != NULL ) ? pVarClient->rr.clientid : -1;

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

                /* send new connection greeting message */
                n = strlen( message );
                s = send( new_client, message, n, 0 );
                if( s != n )
                {
                    perror("send");
                }
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

/*! @}
 * end of sockserver group */
