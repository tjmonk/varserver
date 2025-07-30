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
 * @defgroup varprint varprint
 * @brief Handle inter-process printing of rendered variables
 * @{
 */

/*============================================================================*/
/*!
@file varprint.c

    Variable Printing Engine

    The Variable Printing Engine manages the printing of rendered
    string variables between variable server clients.  Two clients are involved
    in the process, the requestor and the responder.  The responder is
    the client that has registered a PRINT notification against a
    variable and is responsible for producing the content of that variable
    string.  The requestor is the client that is requesting to print
    the content of a rendered variable to an output stream which it
    owns.  The Variable Printing Engine manages co-ordination between
    the two clients and the passing of the file descriptor for the
    output stream from the requestor to the responder.
    The requesting client remains blocked until the responding client
    has completed generating the string output to the requestor's stream.

*/
/*============================================================================*/


/*==============================================================================
        Includes
==============================================================================*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <varserver/varobject.h>
#include <varserver/varclient.h>
#include <varserver/varserver.h>
#include <varserver/var.h>
#include <varserver/varprint.h>

/*==============================================================================
        Private function declarations
==============================================================================*/
static int varprint_SendFd( int sock, int fd );
static int varprint_ReceiveFd( int sock, int *fd );

/*==============================================================================
        File scoped variables
==============================================================================*/

/*==============================================================================
        Function definitions
==============================================================================*/

/*============================================================================*/
/*  VARPRINT_GetFileDescriptor                                                */
/*!

    Get the file descriptor from the requestor

@param[in]
    requestorPID
        process identifier of the requestor

@param[in]
    sock
        socket on which to receive the file descriptor

@param[out]
    fd
        pointer to the location to store the returned file descriptor

@retval EOK the file descriptor was successfully received
@retval EINVAL invalid arguments

==============================================================================*/
int VARPRINT_GetFileDescriptor( int sock,
                                int *fd )
{
    int result = EINVAL;
    int conn;
    fd_set readfds;
    struct timeval timeout;
    int sel;

    /* set up the maximum time to wait for the file descriptor */
    timeout.tv_sec = 0;
    timeout.tv_usec = 200000;

    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    /* wait for a connection from a reomote peer or timeout if
       we don't receive one */
    sel = select( sock + 1, &readfds, NULL, NULL, &timeout );
    if ( sel > 0 && FD_ISSET(sock, &readfds))
    {
        /* accept the connection  */
        conn = accept(sock, NULL, 0);
        if( conn != -1 )
        {
            /* receive the file descriptor */
            result = varprint_ReceiveFd( conn, fd );

            /* close the connection after the credentials are sent */
            close( conn );
        }
        else
        {
            result = errno;
        }
    }
    else if ( sel == 0 )
    {
        /* timeout occurred */
        result = ETIMEDOUT;
    }

    return result;
}

/*============================================================================*/
/*  varprint_ReceiveFd                                                        */
/*!

    Receive a file descriptor over the specified socket

@param[in]
    sock
        socket used to receive the file descriptor

@param[in]
    fd
        pointer to the location to store the received file descriptor

@retval EOK the file descriptor was successfully received
@retval other error result from recvmsg

==============================================================================*/
static int varprint_ReceiveFd( int sock, int *fd )
{
    int result = EINVAL;
    struct msghdr msg;
    struct iovec iov[1];
    struct cmsghdr *cmsg = NULL;
    char ctrl_buf[CMSG_SPACE(sizeof(int))];
    char data[1];
    int n;

    if( fd != NULL )
    {
        /* clear the message header */
        memset(&msg, 0, sizeof(struct msghdr));

        /* clear the message control buffer */
        memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));

        /* populate the message data */
        iov[0].iov_base = data;
        iov[0].iov_len = sizeof(data);

        /* populate the message */
        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_control = ctrl_buf;
        msg.msg_controllen = CMSG_SPACE(sizeof(int));
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;

        /* receive a message from the requestor */
        n = recvmsg( sock, &msg, 0 );

        /* determine if the receive was successful */
        result = ( n >= 0 ) ? EOK : errno;

        if( result == EOK )
        {
            /* extract the CMSG object from the message */
            cmsg = CMSG_FIRSTHDR(&msg);

            /* store the received file descriptor */
            *fd = *((int *) CMSG_DATA(cmsg));
        }
    }

    return result;
}

/*============================================================================*/
/*  VARPRINT_SendFileDescriptor                                               */
/*!
    Send a file descriptor from the requestor to the responder
    The requestor is the client which is requesting the variable
    to be printed on its output stream.  The responder is the
    clent which will be generating the output for the variable
    to the requestor's output stream.

    The requestor calls this function.

@param[in]
    responderPID
        process identifier of the requestor

@param[in]
    fd
        the file descriptor to send

@retval EOK the file descriptor was sent to the responder
@retval other indicates errno from socket, connect, or sendmsg

==============================================================================*/
int VARPRINT_SendFileDescriptor( int responderPID, int fd )
{
    int sock;
    struct sockaddr_un addr;
    int result = EINVAL;

    /* Create a unix domain socket */
    sock = socket( AF_UNIX, SOCK_STREAM, 0 );
    if( sock != -1 )
    {
        /* bind it to the address of the requestor */
        memset( &addr, 0, sizeof(addr) );
        addr.sun_family  = AF_UNIX;
        sprintf( addr.sun_path, "/tmp/client_%d", responderPID );

        /* connect to the requestor */
        while( 1 )
        {
            if( connect( sock, (struct sockaddr *)&addr, sizeof(addr) ) == -1 )
            {
                result = errno;
                break;
            }
            else
            {
                /* Receive the file descriptor */
                result = varprint_SendFd(sock, fd );
                break;
            }
        }

        /* close the socket of the sender */
        close( sock );
    }
    else
    {
        result = errno;
    }

    return result;
}

/*============================================================================*/
/*  VARPRINT_SetupListener                                                    */
/*!
    Set up a listening socket which will be used to send the
    file descriptor from the requestor (requesting the print operation)
    to the responder (performing the print operation).
    The responder calls this function.

@param[in]
    requestorPID
        process identifier of the requestor

@param[out]
    sock
        the socket which will be used to send the file descriptor

@retval EOK the listener was successfully created
@retval other indicates an errno

==============================================================================*/
int VARPRINT_SetupListener( pid_t requestorPID, int *sock, gid_t gid )
{
    int result = EINVAL;
    struct sockaddr_un addr;
    int s;
    int reuse = 1;
    int rc;

    /* Create a unix domain socket */
    s = socket( AF_UNIX, SOCK_STREAM, 0 );
    if( s != -1 )
    {
        /* allow the socket to be reused */
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int) );

        /* Bind it to an address associated with the requesting client */
        memset( &addr, 0, sizeof(addr) );
        addr.sun_family = AF_UNIX;
        sprintf(addr.sun_path, "/tmp/client_%d", requestorPID );

        if( bind(s, (struct sockaddr *)&addr, sizeof(addr)) != -1 )
        {
            /* set up group ownership for client file */
            if ( chown( addr.sun_path, -1, gid) == 0 )
            {
                rc = chmod( addr.sun_path,
                    S_IWUSR | S_IRUSR | S_IXUSR | S_IWGRP | S_IRGRP | S_IXGRP );
                if ( rc != 0 )
                {
                    result = errno;
                }
            }
            else
            {
                result = errno;
            }

            /* listen for a connection */
            if( listen(s, 1) != -1 )
            {
                *sock = s;
                result = EOK;
            }
            else
            {
                result = errno;
            }
        }
        else
        {
            result = errno;
        }
    }
    else
    {
        result = errno;
    }

    return result;
}

/*============================================================================*/
/*  VARPRINT_ShutdownListener                                                 */
/*!
    Shut down the listening socket which was used to transfer
    the file descriptor from the requestor (requesting the print operation)
    to the responder (performing the print operation).
    The responder calls this function.

@param[in]
    requestorPID
        process identifier of the requestor

@param[out]
    sock
        the socket which was used to send the file descriptor

@retval EOK the listener was successfully shut down
@retval other indicates an errno

==============================================================================*/
int VARPRINT_ShutdownListener( pid_t requestorPID, int sock )
{
    int result = EINVAL;
    char path[108];

    if( ( requestorPID != 0 ) &&
        ( sock != -1 ) )
    {
        /* close the listening socket */
        close( sock );

        /* unlink the UNIX DOMAIN socket file */
        sprintf( path, "/tmp/client_%d", requestorPID );
        if( unlink( path ) != -1 )
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

/*============================================================================*/
/*  varprint_SendFd                                                           */
/*!
    Send a file descriptor to the specified connection

    Send a file descriptor on the specified socket using a CMSG
    object to transfer SCM_RIGHTS to the file descriptor to
    the peer process.  This gives the peer process permission to
    read/write our file descriptor.

@param[in]
    sock
        socket on which to send the credentials for the file descriptor

@param[in]
    fd
        file descriptor to send the credentials for

==============================================================================*/
static int varprint_SendFd( int sock, int fd )
{
    struct msghdr msg;
    struct iovec iov[1];
    struct cmsghdr *cmsg = NULL;
    char ctrl_buf[CMSG_SPACE(sizeof(int))];
    char data[1];
    int n;
    int result = EINVAL;

    /* clear the msghdr and message data */
    memset(&msg, 0, sizeof(struct msghdr));
    memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));

    /* we always have to send at least one dummy data byte */
    data[0] = ' ';
    iov[0].iov_base = data;
    iov[0].iov_len = sizeof(data);

    /* construct the message header */
    msg.msg_name = NULL;
    msg.msg_namelen = 0;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_controllen =  CMSG_SPACE(sizeof(int));
    msg.msg_control = ctrl_buf;

    /* construct the CMSG header */
    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));

    /* put the file descriptor in the to CMSG data object */
    *((int *) CMSG_DATA(cmsg)) = fd;

    /* send the message to the peer */
    n = sendmsg(sock, &msg, 0);

    /* determine of the send was successful */
    result = ( n >= 0 ) ? EOK : errno;

    return result;
}

/*! @}
 * end of varprint group */
