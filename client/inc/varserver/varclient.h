/*============================================================================
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
============================================================================*/

#ifndef VARCLIENT_H
#define VARCLIENT_H

/*============================================================================
        Includes
============================================================================*/

#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdbool.h>
#include <mqueue.h>
#include "varobject.h"
#include "varrequest.h"
#include "var.h"

/*============================================================================
        Public definitions
============================================================================*/

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

/*! Server Information shared with clients */
typedef struct _serverInfo
{
    /*! server process identifier */
    pid_t pid;

} ServerInfo;

/*! The requestResponse object is used to send and receive commands and
    responses between the client and the server */
typedef struct _requestResponse
{
    /*! identifier of the var client */
    uint32_t id;

    /*! varserver version */
    uint16_t version;

    /*! client identifier */
    int clientid;

    /*! client process identifier */
    pid_t client_pid;

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

    /*! length of data in workbuf */
    size_t len;

} RequestResponse;

/*! The VarClient structure is used as the primary data structure
    for client/server interactions */
typedef struct _varClient
{
    /*! used for server-side client chaining */
    struct _varClient *pNext;

    /*! pointer to the variable server interface API */
    const void *pAPI;

    /* client request - common data shared between client and server */
    RequestResponse rr;

    /*! semaphore used to synchronize client and server */
    sem_t sem;

    /*! client message queue used to receive notifications */
    mqd_t notificationQ;

    /*! flag indicating to output debug information */
    int debug;

    /*! client socket descriptor */
    int sd;

    /*! client notification descriptor */
    int notify_sd;

    /*! indicate if client is active */
    bool active;

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

    /*! pointer to close handler */
    int (*pFnClose)(struct _varClient *pVarClient);

    /*! pointer to unblocking function */
    int (*pFnUnblock)(struct _varClient *pVarClient);

    /*! pointer to open handler */
    int (*pFnOpen)(struct _varClient *pVarClient);

    /*! specifies the length of the working buffer */
    size_t workbufsize;

    /*! first byte of the working buffer (must be
        the last element in this structure )*/
    char workbuf;

} VarClient;

/*! The VarServerAPI defines the functional interface between the
    VarServer client and server */
typedef struct _VarServerAPI
{
    VARSERVER_HANDLE (*open)( size_t workbufsize );
    int (*close)( VarClient *pVarClient );
    VAR_HANDLE (*findByName)( VarClient *pVarClient, char *pName );
    int (*createvar)( VarClient *pVarClient, VarInfo *pVarInfo );
    int (*test)( VarClient *pVarClient );
    int (*get)( VarClient *pVarClient, VAR_HANDLE hVar, VarObject *pVarObject );
    int (*getvalidationrequest)(VarClient *pVarClient,
                                uint32_t id,
                                VAR_HANDLE *hVar,
                                VarObject *pVarObject );
    int (*sendvalidationresponse)(VarClient *pVarClient,
                                  uint32_t id,
                                  int response );
    int (*getlength)(VarClient *pVarClient, VAR_HANDLE hVar, size_t *len );
    int (*gettype)(VarClient *pVarClient, VAR_HANDLE hVar, VarType *pVarType );
    int (*getname)(VarClient *pVarClient,
                   VAR_HANDLE hVar,
                   char *buf,
                   size_t len );
    int (*set)(VarClient *pVarClient, VAR_HANDLE hVar, VarObject *pVarObject);
    int (*first)(VarClient *pVarClient, VarQuery *query, VarObject *obj);
    int (*next)(VarClient *pVarClient, VarQuery *query, VarObject *obj);
    int (*notify)(VarClient *pVarClient,
                  VAR_HANDLE hVar,
                  NotificationType notificationType);
    int (*print)(VarClient *pVarClient, VAR_HANDLE hVar, int fd);
    int (*ops)(VarClient *pVarClient, uint32_t id, VAR_HANDLE *hVar, int *fd);
    int (*cps)(VarClient *pVarClient, uint32_t id, int fd);
} VarServerAPI;

#endif
