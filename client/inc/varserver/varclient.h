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

#ifndef VARCLIENT_H
#define VARCLIENT_H

/*==============================================================================
        Includes
==============================================================================*/

#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include <mqueue.h>
#include "varobject.h"
#include "var.h"

/*==============================================================================
        Public definitions
==============================================================================*/

/*! Signal used to create a new client */
#define SIG_NEWCLIENT SIGRTMIN+1

/*! Signal used to pass a client request */
#define SIG_CLIENT_REQUEST SIGRTMIN+2

/*! Signal used to pass a client response */
#define SIG_CLIENT_RESPONSE SIGRTMIN+3

/*! Signal used to pass a client notification */
#define SIG_VARCLIENT_NOTIFICATION SIGRTMIN+4

/*! Success response */
#ifndef EOK
#define EOK 0
#endif

/*! Name of the server API endpoint */
#define SERVER_SHAREDMEM "/varserver"

/*! identifier for the var server */
#define VARSERVER_ID  ( 0x56415253 )

/*! protocol version of the var server to check for library/server mismatch */
#define VARSERVER_VERSION ( 1 )

#ifndef VARSERVER_MAX_GROUPS
/*! maximum number of groups the varserver supports */
#define VARSERVER_MAX_GROUPS 10
#endif

/*! handle to the variable server */
typedef void * VARSERVER_HANDLE;

/*! the VarRequest enumeration specifies the type of requests that can
    be made from the client to the server */
typedef enum _varRequest
{
    /*! invalid VAR request */
    VARREQUEST_INVALID=0,

    /*! A new client is requesting an interface to the variable server */
    VARREQUEST_OPEN,

    /*! A client is requesting to terminate its interface
        to the variable server */
    VARREQUEST_CLOSE,

    /*! echo test */
    VARREQUEST_ECHO,

    /*! New Variable request */
    VARREQUEST_NEW,

    /*! Alias Variable request */
    VARREQUEST_ALIAS,

    /*! Get Variable Alias list */
    VARREQUEST_GET_ALIASES,

    /*! Find Variable request */
    VARREQUEST_FIND,

    /*! Get Variable value */
    VARREQUEST_GET,

    /*! Print Variable Value */
    VARREQUEST_PRINT,

    /*! Set Variable Value */
    VARREQUEST_SET,

    /*! Get Variable Type */
    VARREQUEST_TYPE,

    /*! Get Variable Name */
    VARREQUEST_NAME,

    /*! Get Variable Length */
    VARREQUEST_LENGTH,

    /*! Get Variable Flags */
    VARREQUEST_FLAGS,

    /*! Get Variable Info */
    VARREQUEST_INFO,

    /*! Request Variable Notification */
    VARREQUEST_NOTIFY,

    /*! Cancel Variable Notification */
    VARREQUEST_NOTIFY_CANCEL,

    /*! Request Validation Request Info */
    VARREQUEST_GET_VALIDATION_REQUEST,

    /*! Send a Validation Response */
    VARREQUEST_SEND_VALIDATION_RESPONSE,

    /*! Open a print session */
    VARREQUEST_OPEN_PRINT_SESSION,

    /*! Close a print session */
    VARREQUEST_CLOSE_PRINT_SESSION,

    /*! Get the first variable in a search query result */
    VARREQUEST_GET_FIRST,

    /*! Get the next variable in a search query result */
    VARREQUEST_GET_NEXT,

    /*! Set variable flags */
    VARREQUEST_SET_FLAGS,

    /*! Set variable flags */
    VARREQUEST_CLEAR_FLAGS,

    /*! End request type marker */
    VARREQUEST_END_MARKER

} VarRequest;


/*! Server Information shared with clients */
typedef struct _serverInfo
{
    /*! server process identifier */
    pid_t pid;

} ServerInfo;


/*! The VarClient structure is used as the primary data structure
    for client/server interactions */
typedef struct _varClient
{
    /*! identifier of the var client */
    uint32_t id;

    /*! varserver version */
    uint16_t version;

    /*! semaphore used to synchronize client and server */
    sem_t sem;

    /*! time structure used to set timeouts for semaphores */
    struct timespec ts;

    /*! client message queue used to receive notifications */
    mqd_t notificationQ;

    /*! flag indicating to output debug information */
    int debug;

    /*! client identifier */
    int clientid;

    /*! client process identifier */
    pid_t client_pid;

    /*! varclient effective user id */
    uid_t uid;

    /*! varclient effective group id */
    gid_t gid;

    /*! varserver group id */
    gid_t varserver_gid;

    /*! number of groups in the group list */
    int ngroups;

    /*! maxmimum number of groups supported by varserver */
    gid_t grouplist[ VARSERVER_MAX_CLIENT_GIDS ];

    /*! process identifier of a peer process which is
        interacting with this one, eg during variable validation */
    pid_t peer_pid;

    /*! type of request */
    VarRequest requestType;

    /*! Variable storage */
    VarInfo variableInfo;

    /*! request Value */
    int requestVal;

    /*! response value */
    int responseVal;

    /*! indicates a request is already in progress */
    bool validationInProgress;

    /*! signal mask */
    sigset_t mask;

    /*! pointer to the server information */
    ServerInfo *pServerInfo;

    /*! client transaction counter */
    uint64_t transactionCount;

    /*! client blocked 0=not blocked non-zero=blocked */
    int blocked;

    /*! specifies the length of the working buffer */
    size_t workbufsize;

    /*! first byte of the working buffer (must be
        the last element in this structure )*/
    char workbuf;

} VarClient;

/*==============================================================================
        Public function declarations
==============================================================================*/

VarClient *ValidateHandle( VARSERVER_HANDLE hVarServer );

int ClientRequest( VarClient *pVarClient, int signal );

#endif
