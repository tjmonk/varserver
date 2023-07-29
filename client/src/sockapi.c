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
 * @defgroup sockapi VarServer Socket API
 * @brief RealTime In-Memory Publish/Subscribe Key/Value store socket API
 * @{
 */

/*============================================================================*/
/*!
@file sock.c

    Variable Server Shared Memory API

    The Variable Server Shared Memory API is an Application Programming
    Interface to the real time in-memory pub/sub key/value store,

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
#include <sys/mman.h>
#include <sys/syslog.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <varserver/varobject.h>
#include <varserver/varclient.h>
#include <varserver/varserver.h>
#include <varserver/varprint.h>
#include <varserver/var.h>
#include <varserver/sockapi.h>

/*==============================================================================
        Private type declarations
==============================================================================*/

/*! the VarServerAddress object specifies the network connectivity
    information for the VarServer */
typedef struct varserverAddress
{
    /* varserver address */
    struct sockaddr_in addr;

    /* varserver port */
    uint16_t port;
} VarserverAddress;

/*==============================================================================
        Private function declarations
==============================================================================*/

static VARSERVER_HANDLE open_varserver( size_t workbufsize );
static int varserver_GetAddress( VarserverAddress *pVarserverAddress );
static int close_varserver( VarClient *pVarClient );
static VAR_HANDLE findByName( VarClient *pVarClient, char *pName );
static int createVar( VarClient *pVarClient, VarInfo *pVarInfo );
static int test( VarClient *pVarClient );
static int get( VarClient *pVarClient, VAR_HANDLE hVar, VarObject *pVarObject );
static int getValidationRequest( VarClient *pVarClient,
                                 uint32_t id,
                                 VAR_HANDLE *hVar,
                                 VarObject *pVarObject );
static int sendValidationResponse( VarClient *pVarClient,
                                   uint32_t id,
                                   int response  );
static int getLength( VarClient *pVarClient,
                      VAR_HANDLE hVar,
                      size_t *len );

static int getType( VarClient *pVarClient,
                    VAR_HANDLE hVar,
                    VarType *pVarType );

static int getName( VarClient *hVarServer,
                    VAR_HANDLE hVar,
                    char *buf,
                    size_t buflen );

static int set( VarClient *pVarClient,
                VAR_HANDLE hVar,
                VarObject *pVarObject );

static int getFirst( VarClient *pVarClient,
                     VarQuery *query,
                     VarObject *obj );

static int getNext( VarClient *pVarClient,
                    VarQuery *query,
                    VarObject *obj );

static int notify( VarClient *pVarClient,
                   VAR_HANDLE hVar,
                   NotificationType notificationType );

static int print( VarClient *pVarClient,
                  VAR_HANDLE hVar,
                  int fd );

static int openPrintSession( VarClient *pVarClient,
                             uint32_t id,
                             VAR_HANDLE *hVar,
                             int *fd );

static int closePrintSession( VarClient *pVarClient,
                              uint32_t id,
                              int fd );

static int ClientRequest( VarClient *pVarClient, int request );

static int ClientCleanup( VarClient *pVarClient );
static int var_PrintValue( int fd, VarInfo *pInfo, char *workbuf );
static int var_GetVarObject( VarClient *pVarClient, VarObject *pVarObject );
static int var_GetBlobObjectFromWorkbuf( VarClient *pVarClient,
                                         VarObject *pVarObject );
static int var_GetStringObjectFromWorkbuf( VarClient *pVarClient,
                                           VarObject *pVarObject );
static int var_CopyStringVarObjectToWorkbuf( VarClient *pVarClient,
                                             VarObject *pVarObject );
static int var_CopyBlobVarObjectToWorkbuf( VarClient *pVarClient,
                                        VarObject *pVarObject );


static VARSERVER_HANDLE varserver_OpenRemote( size_t workbufsize );
static VARSERVER_HANDLE varserver_OpenLocal( size_t workbufsize );

static int writesd( int sd, char *p, size_t len );
static int readsd( int sd, char *p, size_t len );

/*==============================================================================
        File scoped variables
==============================================================================*/

static const VarServerAPI sockapi = {
    open_varserver, close_varserver, findByName, createVar, test, get,
    getValidationRequest, sendValidationResponse, getLength,
    getType, getName, set, getFirst, getNext, notify, print,
    openPrintSession, closePrintSession };

/*==============================================================================
        Function definitions
==============================================================================*/

/*============================================================================*/
/*  SOCKAPI                                                                   */
/*!
    Get the VarServer socket APIs

    The SOCKAPI function is used to get the VarServer socket APIs
    This is an abstraction which allows the varserver client to easily
    switch between different communication mechanisms with the server.

    @retval pointer to the varserver socket APIs

==============================================================================*/
const VarServerAPI *SOCKAPI( void )
{
    return &sockapi;
}

/*============================================================================*/
/*  open_varserver                                                            */
/*!
    Open a connection to the variable server

    The open function is used to open a connection to the variable server
    via a socket.

    @param[in]
        workbufsize
            size of the client's working buffer

    @retval handle to the variable server
    @retval NULL if the remote variable server could not be opened

==============================================================================*/
static VARSERVER_HANDLE open_varserver( size_t workbufsize )
{
    int result = EINVAL;
    VarserverAddress vsa;
    VarClient *pVarClient = NULL;
    SockRequest req;
    size_t len = sizeof(SockRequest);
    int sd;
    int rc;

    /* get the variable server address information */
    result = varserver_GetAddress( &vsa );
    if ( result == EOK )
    {
        /* create a communication socket */
        sd = socket( AF_INET, SOCK_STREAM, 0 );
        if ( sd < 0 )
        {
            result = errno;
        }
        else
        {
            /* connect to the remote */
            rc = connect( sd, (struct sockaddr *)&vsa.addr, sizeof(vsa.addr));
            if ( rc < 0 )
            {
                result = errno;
            }
            else
            {
                /* allocate memory for the VarClient */
                pVarClient = calloc( 1, sizeof( VarClient ) + workbufsize );
                if ( pVarClient != NULL )
                {
                    pVarClient->sd = sd;
                    pVarClient->rr.id = VARSERVER_ID;
                    pVarClient->rr.version = VARSERVER_VERSION;
                    pVarClient->rr.client_pid = getpid();
                    pVarClient->workbufsize = workbufsize + 1;
                    pVarClient->pAPI = &sockapi;
                    pVarClient->rr.requestType = VARREQUEST_OPEN;
                    pVarClient->rr.len = workbufsize;
                    ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
                }
                else
                {
                    close( sd );
                }
            }
        }
    }

    return pVarClient;
}

/*============================================================================*/
/*  varserver_GetAddress                                                      */
/*!
    Get the Variable Server network connectivity information

    The varserver_GetAddress function is used to get the variable server
    network connectivity information.  That is the IP address and port
    number of the variable server.

    This information is retrieved from the VARSERVER_ADDRESS and
    VARSERVER_PORT environment variables

    @param[in,out]
        pVarserverAddress
            pointer to a VarserverAddress object to initialize

    @retval EOK the VarserverAddress object was populated
    @retval EINVAL invalid arguments
    @retval ENOTSUP address format not supported
    @retval ENOENT address information not found

==============================================================================*/
static int varserver_GetAddress( VarserverAddress *pVarserverAddress )
{
    char *addr;
    char *portnum;
    int port;
    int result = EINVAL;
    int rc;

    if ( pVarserverAddress != NULL )
    {
        addr = getenv("VARSERVER_ADDRESS");
        portnum = getenv("VARSERVER_PORT");

        result = ENOENT;

        if ( ( addr != NULL ) && ( portnum != NULL ) )
        {
            result = ENOTSUP;

            rc = inet_pton( AF_INET,
                            addr,
                            &pVarserverAddress->addr.sin_addr );
            if ( rc == 1 )
            {
                port = atoi( portnum );
                if ( ( port > 0 ) && ( port <= 65535 ) )
                {
                    pVarserverAddress->port = port;
                    pVarserverAddress->addr.sin_family = AF_INET;
                    pVarserverAddress->addr.sin_port = htons(port);

                    result = EOK;
                }
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  close_varserver                                                           */
/*!
    Close the connection to the variable server

    The VARSERVER_Close function is used by the variable server clients
    to disconnect from the variable server and clean up all resources
    used for the connection.

    @param[in]
        pVarClient
            pointer to the variable server client

    @retval EOK - the connection was successfully closed
    @retval EINVAL - an invalid variable server handle was specified

==============================================================================*/
static int close_varserver( VarClient *pVarClient )
{
    int result = EINVAL;

    if( pVarClient != NULL )
    {
        pVarClient->rr.requestType = VARREQUEST_CLOSE;
        pVarClient->rr.len = 0;

        ClientRequest( pVarClient, SIG_CLIENT_REQUEST );

        /* clean up the Var client */
        ClientCleanup( pVarClient );

        /* indicate success */
        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  createVar                                                                 */
/*!
    Create a new variable

    The createVar function sends a request to the variable
    server to create a new variable.

    @param[in]
        pVarClient
            pointer to the variable server client

    @param[out]
        pVarInfo
            pointer to the VarInfo object containing information
            about the variable to be created

    @param[out]
        pLen
            pointer to the location to store the working buffer length

    @retval EOK - the working buffer pointer was successfully retrieved
    @retval EINVAL - invalid arguments

==============================================================================*/
static int createVar( VarClient *pVarClient, VarInfo *pVarInfo )
{
    int result = EINVAL;
    int rc;

    if( ( pVarClient != NULL ) &&
        ( pVarInfo != NULL ) )
    {
        /* copy the variable information */
        memcpy( &pVarClient->rr.variableInfo, pVarInfo, sizeof( VarInfo ) );

        pVarClient->rr.requestType = VARREQUEST_NEW;
        pVarClient->rr.len = 0;

        rc = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( rc == EOK )
        {
            result = pVarClient->rr.responseVal;
        }
        else
        {
            result = rc;
        }

    }

    return result;
}

/*============================================================================*/
/*  test                                                                      */
/*!
    Test the connection to the variable server

    The test function is used by the variable server clients
    to test the connection to the variable server and exercise the
    API.

    @param[in]
        pVarClient
            pointer to the varserver client

    @retval EOK - the connection was successfully closed
    @retval EINVAL - an invalid variable server handle was specified

==============================================================================*/
static int test( VarClient *pVarClient )
{
    int result = EINVAL;
    int i;

    if( pVarClient != NULL )
    {
        for(i=0;i<100;i++)
        {
            pVarClient->rr.requestVal = i;
            pVarClient->rr.requestType = VARREQUEST_ECHO;
            pVarClient->rr.len = 0;

            ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
            if( pVarClient->debug >= LOG_DEBUG )
            {
                printf("Client %d sent %d and received %d\n",
                    pVarClient->rr.clientid,
                    pVarClient->rr.requestVal,
                    pVarClient->rr.responseVal);
            }
        }

        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  findByName                                                                */
/*!
    Find a variable given its name

    The findByName function requests the handle for the specified
    variable from the variable server.

    @param[in]
        pVarClient
            pointer to the varserver client

    @param[in]
        pName
            pointer to the variable name

    @retval handle of the variable
    @retval VAR_INVALID if the variable cannot be found

==============================================================================*/
static VAR_HANDLE findByName( VarClient *pVarClient, char *pName )
{
    VAR_HANDLE hVar = VAR_INVALID;
    int rc;
    size_t len;

    if( ( pVarClient != NULL ) &&
        ( pName != NULL ) )
    {
        len = strlen(pName);
        if( len < MAX_NAME_LEN )
        {
            /* copy the name to the variable info request */
            strcpy(pVarClient->rr.variableInfo.name, pName );
            pVarClient->rr.variableInfo.instanceID = 0;

            /* specify the request type */
            pVarClient->rr.requestType = VARREQUEST_FIND;
            pVarClient->rr.len = 0;

            rc = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
            if( rc == EOK )
            {
                hVar = (VAR_HANDLE)pVarClient->rr.responseVal;
            }
        }
    }

    return hVar;
}

/*============================================================================*/
/*  get                                                                       */
/*!
    Get a variable value and store it in the specified var object

    The get function gets the value of the variable
    specified by hVar and puts it into the specified var object

    @param[in]
        pVarClient
            pointer to the varserver client

    @param[in]
        hVar
            handle to the variable to be retrieved

    @param[in]
        pVarObject
            specifies the location where the variable value should be stored

    @retval EOK - the variable was retrieved ok
    @retval EINVAL - invalid arguments

==============================================================================*/
static int get( VarClient *pVarClient, VAR_HANDLE hVar, VarObject *pVarObject )
{
    int result = EINVAL;
    int n;

    if( ( pVarClient != NULL ) &&
        ( pVarObject != NULL ) )
    {
        pVarClient->rr.requestType = VARREQUEST_GET;
        pVarClient->rr.variableInfo.hVar = hVar;
        pVarClient->rr.len = 0;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            result = var_GetVarObject( pVarClient, pVarObject );
        }
    }

    return result;
}

/*============================================================================*/
/*  getValidationRequest                                                      */
/*!
    Get a information about a validation request

    The getValidationRequest function gets the the validation
    request specified by the validation request identifier.
    The returned VarObject will contain the proposed variable
    change requested by the other client.

    @param[in]
        pVarClient
            pointer to the varserver client

    @param[in]
        id
            identifier of the validation request

    @param[out]
        hVar
            handle of the variable to be validated

    @param[out]
        pVarObject
            specifies the location where the variable value should be stored

    @retval EOK - the validation request was retrieved ok
    @retval ENOMEM - cannot allocate memory for the string (string var only)
    @retval EINVAL - invalid arguments

==============================================================================*/
static int getValidationRequest( VarClient *pVarClient,
                                 uint32_t id,
                                 VAR_HANDLE *hVar,
                                 VarObject *pVarObject )
{
    int result = EINVAL;

    if( ( pVarClient != NULL ) &&
        ( pVarObject != NULL ) &&
        ( hVar != NULL ) )
    {
        pVarClient->rr.requestType = VARREQUEST_GET_VALIDATION_REQUEST;
        pVarClient->rr.requestVal = id;
        pVarClient->rr.len = 0;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            result = var_GetVarObject( pVarClient, pVarObject );
            if( result == EOK )
            {
                /* get the handle of the variable to be validated */
                *hVar = pVarClient->rr.variableInfo.hVar;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  sendValidationResponse                                                    */
/*!
    Send a Validation response

    The sendValidationResponse function sends a validation response
    for the specified validation reqeust.

    @param[in]
        pVarClient
            pointer to the varserver client

    @param[in]
        id
            identifier of the validation request

    @param[in]
        response
            EOK - the validation was successful
            EINVAL - the validation was unsuccessful

    @retval EOK - the validation request was retrieved ok
    @retval EINVAL - invalid arguments

==============================================================================*/
static int sendValidationResponse( VarClient *pVarClient,
                                   uint32_t id,
                                   int response  )
{
    int result = EINVAL;

    if( pVarClient != NULL )
    {
        pVarClient->rr.requestType = VARREQUEST_SEND_VALIDATION_RESPONSE;
        pVarClient->rr.requestVal = id;
        pVarClient->rr.responseVal = response;
        pVarClient->rr.len = 0;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
    }

    return result;
}

/*============================================================================*/
/*  var_GetVarObject                                                          */
/*!
    Get a variable object from the variable client

    The var_GetVarObject function retrieves a copy of the client's
    VarObject which is returned from the server.  If the object
    type is a string, and the destination VarObject has an existing
    string buffer, the string is copied into the VarObject's string buffer.
    If the VarObject does not have a string buffer, one is automatically
    allocated and is the responsibility of the caller to deallocate
    the string buffer.

    If the object type is a blob, and the destination VarObject has an
    existing blob buffer, then the blob is copied into the VarObject's blob
    buffer.  If the VarObject does not have a blob buffer, one is automatically
    allocated and is the responsibility of the caller to deallocate
    the blob buffer.

    @param[in]
        pVarClient
            pointer to the Variable Client which contains the source VarObject

    @param[in]
        VarObject
            pointer to the destination VarObject

    @retval EOK - the validation request was retrieved ok
    @retval ENOMEM - cannot allocate memory for the object (string/blob only)
    @retval E23BIG - not enough space to store the string/blob variable
    @retval ENOTSUP - incorrect type match
    @retval EINVAL - invalid arguments

==============================================================================*/
static int var_GetVarObject( VarClient *pVarClient, VarObject *pVarObject )
{
    int result = EINVAL;
    size_t srclen;

    if( ( pVarClient != NULL ) &&
        ( pVarObject != NULL ) )
    {
        /* set the destination object type */
        pVarObject->type = pVarClient->rr.variableInfo.var.type;
        if( pVarObject->type == VARTYPE_STR )
        {
            /* get the string from the work buffer */
            result = var_GetStringObjectFromWorkbuf( pVarClient, pVarObject );
        }
        else if ( pVarObject->type == VARTYPE_BLOB )
        {
            /* get the blob from the work buffer */
            result = var_GetBlobObjectFromWorkbuf( pVarClient, pVarObject );
        }
        else
        {
            /* copy primitive type */
            pVarObject->val = pVarClient->rr.variableInfo.var.val;

            /* get the source object length */
            srclen = pVarClient->rr.variableInfo.var.len;

            pVarObject->len = srclen;

            result = EOK;
        }
    }

    return result;
}

/*============================================================================*/
/*  var_GetStringObjectFromWorkbuf                                            */
/*!
    Get a string object from the variable client work buffer

    The var_GetStringObjectFromWorkbuf function retrieves a copy of the client's
    VarObject which is returned from the server.  If the object
    type is a string, and the destination VarObject has an existing
    string buffer, the string is copied into the VarObject's string buffer.
    If the VarObject does not have a string buffer, one is automatically
    allocated and is the responsibility of the caller to deallocate
    the string buffer.

    @param[in]
        pVarClient
            pointer to the Variable Client which contains the source VarObject

    @param[in]
        VarObject
            pointer to the destination VarObject

    @retval EOK - the validation request was retrieved ok
    @retval ENOMEM - cannot allocate memory for the object (string/blob only)
    @retval E23BIG - not enough space to store the string/blob variable
    @retval ENOTSUP - incorrect type match
    @retval EINVAL - invalid arguments

==============================================================================*/
static int var_GetStringObjectFromWorkbuf( VarClient *pVarClient,
                                           VarObject *pVarObject )
{
    int result = EINVAL;
    size_t srclen;
    char *pSrcString;

    if ( ( pVarClient != NULL ) &&
         ( pVarObject != NULL ) )
    {
        /* set the destination object type */
        pVarObject->type = pVarClient->rr.variableInfo.var.type;
        if ( pVarObject->type == VARTYPE_STR )
        {
            /* get the source string */
            pVarClient->rr.variableInfo.var.val.str = &pVarClient->workbuf;
            pSrcString = pVarClient->rr.variableInfo.var.val.str;

            if( pSrcString != NULL )
            {
                /* get the source object length */
                srclen = pVarClient->rr.variableInfo.var.len;

                if( pVarObject->val.str == NULL )
                {
                    /* allocate memory for the target string */
                    pVarObject->val.str = calloc( 1, srclen );
                    pVarObject->len = srclen;
                }
                else
                {
                    /* calculate the length of the source string */
                    srclen = strlen( pSrcString ) + 1;
                }

                if( pVarObject->val.str != NULL )
                {
                    if( pVarObject->len >= srclen )
                    {
                        /* get string from the working buffer */
                        strcpy( pVarObject->val.str,
                                &pVarClient->workbuf );
                        result = EOK;
                    }
                    else
                    {
                        /* not enough space to store the string */
                        result = E2BIG;
                    }
                }
                else
                {
                    /* no memory available for the string result */
                    result = ENOMEM;
                }
            }
            else
            {
                /* should not see this.  The source object says it is a string
                    but it does not have a string pointer */
                result = ENOTSUP;
            }
        }
        else
        {
            result = ENOTSUP;
        }
    }

    return result;
}

/*============================================================================*/
/*  var_GetBlobObjectFromWorkbuf                                              */
/*!
    Get a blob object from the variable client work buffer

    The var_GetBlobObjectFromWorkbuf function retrieves a copy of the client's
    VarObject which is returned from the server.  If the object
    type is a blob, and the destination VarObject has an existing
    blob buffer, the blob is copied into the VarObject's blob buffer.
    If the VarObject does not have a blob buffer, one is automatically
    allocated and is the responsibility of the caller to deallocate
    the blob buffer.

    @param[in]
        pVarClient
            pointer to the Variable Client which contains the source VarObject

    @param[in]
        VarObject
            pointer to the destination VarObject

    @retval EOK - the validation request was retrieved ok
    @retval ENOMEM - cannot allocate memory for the object (string/blob only)
    @retval E23BIG - not enough space to store the string/blob variable
    @retval ENOTSUP - incorrect type match
    @retval EINVAL - invalid arguments

==============================================================================*/
static int var_GetBlobObjectFromWorkbuf( VarClient *pVarClient,
                                         VarObject *pVarObject )
{
    int result = EINVAL;
    size_t srclen;
    void *pSrcBlob;

    if ( ( pVarClient != NULL ) &&
         ( pVarObject != NULL ) )
    {
        /* set the destination object type */
        pVarObject->type = pVarClient->rr.variableInfo.var.type;
        if ( pVarObject->type == VARTYPE_BLOB )
        {
            /* get the source blob */
            pVarClient->rr.variableInfo.var.val.blob = &pVarClient->workbuf;
            pSrcBlob = pVarClient->rr.variableInfo.var.val.blob;

            if( pSrcBlob != NULL )
            {
                /* get the source object length */
                srclen = pVarClient->rr.variableInfo.var.len;

                if( pVarObject->val.blob == NULL )
                {
                    /* allocate memory for the target blob */
                    pVarObject->val.blob = calloc( 1, srclen );
                    pVarObject->len = srclen;
                }

                if( pVarObject->val.blob != NULL )
                {
                    if( pVarObject->len >= srclen )
                    {
                        /* get blob from the working buffer */
                        memcpy( pVarObject->val.blob,
                                &pVarClient->workbuf,
                                srclen );
                        result = EOK;
                    }
                    else
                    {
                        /* not enough space to store the blob */
                        result = E2BIG;
                    }
                }
                else
                {
                    /* no memory available for the blob result */
                    result = ENOMEM;
                }
            }
            else
            {
                /* should not see this.  The source object says it is a blob
                    but it does not have a blob pointer */
                result = ENOTSUP;
            }
        }
        else
        {
            /* not a blob type */
            result = ENOTSUP;
        }
    }

    return result;
}

/*============================================================================*/
/*  getLength                                                                 */
/*!
    Get the length of the specified variable

    The getLength function queries the variable server for the
    length of the specified variable.  Typically this is only useful
    for strings and blobs because the lengths of the other data types could
    easily be calculated directly.

    @param[in]
        pVarClient
            pointer to the varserver client

    @param[in]
        hVar
            handle to the variable to be retrieved

    @param[in]
        len
            specifies the location where the length should be stored

    @retval EOK - the length was retrieved ok
    @retval EINVAL - invalid arguments

==============================================================================*/
static int getLength( VarClient *pVarClient,
                      VAR_HANDLE hVar,
                      size_t *len )
{
    int result = EINVAL;

    if( ( pVarClient != NULL ) &&
        ( len != NULL ) )
    {
        pVarClient->rr.requestType = VARREQUEST_LENGTH;
        pVarClient->rr.variableInfo.hVar = hVar;
        pVarClient->rr.len = 0;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            *len = pVarClient->rr.variableInfo.var.len;
        }
    }
}

/*============================================================================*/
/*  getType                                                                   */
/*!
    Get the variable data type

    The getType function gets the type of the variable
    specified by hVar and puts it into the specified VarType object

    @param[in]
        pVarClient
            pointer to the varserver client

    @param[in]
        hVar
            handle to the variable to be retrieved

    @param[out]
        pVarType
            pointer to the VarType object to populate

    @retval EOK - the variable type was retrieved ok
    @retval EINVAL - invalid arguments

==============================================================================*/
static int getType( VarClient *pVarClient,
                    VAR_HANDLE hVar,
                    VarType *pVarType )
{
    int result = EINVAL;

    if( ( pVarClient != NULL ) &&
        ( pVarType != NULL ) )
    {
        pVarClient->rr.requestType = VARREQUEST_TYPE;
        pVarClient->rr.variableInfo.hVar = hVar;
        pVarClient->rr.len = 0;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            *pVarType = pVarClient->rr.variableInfo.var.type;
        }
        else
        {
            *pVarType = VARTYPE_INVALID;
        }
    }

    return result;
}

/*============================================================================*/
/*  getName                                                                   */
/*!
    Get the variable name given its handle

    The getname function gets the name of the variable
    specified by hVar and puts it into the specified buffer

    @param[in]
        pVarClient
            pointer to the varserver client

    @param[in]
        hVar
            handle to the variable to be retrieved

    @param[in,out]
        buf
            pointer to the buffer to store the variable name

    @param[in]
        buflen
            the length of the buffer to store the variable name

    @retval EOK - the variable name was retrieved ok
    @retval E2BIG - the variable name is too big for the specified buffer
    @retval EINVAL - invalid arguments

==============================================================================*/
static int getName( VarClient *pVarClient,
                    VAR_HANDLE hVar,
                    char *buf,
                    size_t buflen )
{
    int result = EINVAL;

    if( ( pVarClient != NULL ) &&
        ( buf != NULL ) &&
        ( buflen > 0 ) &&
        ( hVar != VAR_INVALID ) )
    {
        pVarClient->rr.requestType = VARREQUEST_NAME;
        pVarClient->rr.variableInfo.hVar = hVar;
        pVarClient->rr.len = 0;

        /* make a request to the server to get the variable name */
        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            /* check if the specified buffer is large enough for the name */
            if ( buflen >= strlen( pVarClient->rr.variableInfo.name ) )
            {
                /* copy the variable name into the supplied buffer */
                strcpy( buf, pVarClient->rr.variableInfo.name );

                /* indicate success */
                result = EOK;
            }
            else
            {
                /* the buffer is not big enough for the variable name */
                result = E2BIG;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  set                                                                       */
/*!
    Set a variable value in the specified var object

    The set function sets the value of the variable
    specified by hVar to the value specified by the var object

    @param[in]
        pVarClient
            pointer to the varserver client

    @param[in]
        hVar
            handle to the variable to be set

    @param[in]
        pVarObject
            pointer to the variable value object to set

    @retval EOK - the variable was set ok
    @retval EINVAL - invalid arguments

==============================================================================*/
static int set( VarClient *pVarClient,
                VAR_HANDLE hVar,
                VarObject *pVarObject )
{
    int result = EINVAL;
    char *p;
    size_t len;

    if( ( pVarClient != NULL ) &&
        ( pVarObject != NULL ) )
    {
        pVarClient->rr.requestType = VARREQUEST_SET;
        pVarClient->rr.variableInfo.hVar = hVar;
        pVarClient->rr.variableInfo.var.type = pVarObject->type;
        pVarClient->rr.variableInfo.var.val = pVarObject->val;
        pVarClient->rr.variableInfo.var.len = pVarObject->len;

        /* strings have to be transferred via the working buffer */
        if( pVarObject->type == VARTYPE_STR )
        {
            var_CopyStringVarObjectToWorkbuf( pVarClient, pVarObject );
        }

        if ( pVarObject->type == VARTYPE_BLOB )
        {
            var_CopyBlobVarObjectToWorkbuf( pVarClient, pVarObject );
        }

        /* send the request to the server */
        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            result = pVarClient->rr.responseVal;
        }
    }

    return result;
}

/*============================================================================*/
/*  var_CopyStringVarObjectToWorkbuf                                          */
/*!
    Copy a string object to the working buffer

    The var_CopyStringVarObjectToWorkbuf function copies the string
    object into the client's working buffer for transfer to the server.

    @param[in]
        pVarClient
            pointer to the VarClient object containing the working buffer

    @param[in]
        pVarObject
            pointer to the VarObject containing the string to copy

    @retval EOK - the string was copied successfully
    @retval EINVAL - invalid arguments
    @retval ENOTSUP - not a string object

==============================================================================*/
static int var_CopyStringVarObjectToWorkbuf( VarClient *pVarClient,
                                             VarObject *pVarObject )
{
    int result = EINVAL;
    size_t len;
    char *p;

    if ( ( pVarClient != NULL ) &&
         ( pVarObject != NULL ) )
    {
        if ( pVarObject->type == VARTYPE_STR )
        {
            /* get the string length */
            len = pVarObject->len;
            if( ( len > 0 ) &&
                ( len < pVarClient->workbufsize ) )
            {
                /* copy the string into the working buffer */
                p = &pVarClient->workbuf;
                memcpy( p, pVarObject->val.str, len );

                /* NUL terminate the string */
                p[len] = 0;

                /* specify the current working buffer useage in bytes */
                pVarClient->rr.len = len+1;

                result = EOK;
            }
        }
        else
        {
            result = ENOTSUP;
        }
    }

    return result;
}

/*============================================================================*/
/*  var_CopyBlobVarObjectToWorkbuf                                            */
/*!
    Copy a blob object to the working buffer

    The var_CopyBlobVarObjectToWorkbuf function copies the blob
    object into the client's working buffer for transfer to the server.

    @param[in]
        pVarClient
            pointer to the VarClient object containing the working buffer

    @param[in]
        pVarObject
            pointer to the VarObject containing the blob to copy

    @retval EOK - the blob was copied successfully
    @retval EINVAL - invalid arguments
    @retval ENOTSUP - not a string object

==============================================================================*/
static int var_CopyBlobVarObjectToWorkbuf( VarClient *pVarClient,
                                           VarObject *pVarObject )
{
    int result = EINVAL;
    size_t len;
    char *p;

    if ( ( pVarClient != NULL ) &&
         ( pVarObject != NULL ) )
    {
        if ( pVarObject->type == VARTYPE_BLOB )
        {
            /* get the blob length */
            len = pVarObject->len;
            if( ( len > 0 ) &&
                ( len < pVarClient->workbufsize ) )
            {
                /* copy the blob into the working buffer */
                p = &pVarClient->workbuf;
                memcpy( p, pVarObject->val.blob, len );

                /* specify the current working buffer usage in bytes */
                pVarClient->rr.len = len;
                result = EOK;
            }
        }
        else
        {
            result = ENOTSUP;
        }
    }

    return result;
}


/*============================================================================*/
/*  getFirst                                                                  */
/*
    Start a variable query

    The getFirst function initiates a variable query with the variable
    server.

    Variable queries can be made using a combination of the following:

    - variable name
    - variable instance ID
    - variable flags
    - variable tags

    @param[in]
        pVarClient
            pointer to the varserver client

    @param[in]
        query
            pointer to the query to be made

    @param[in]
        obj
            pointer to the found variable information

    @retval EOK - a match was found
    @retval EINVAL - invalid arguments
    @retval ENOENT - no matching variable was found

==============================================================================*/
static int getFirst( VarClient *pVarClient,
                     VarQuery *query,
                     VarObject *obj )
{
    int result = EINVAL;
    char *p;
    size_t len;

    if( ( pVarClient != NULL ) &&
        ( query != NULL ) &&
        ( obj != NULL ) )
    {
        pVarClient->rr.requestType = VARREQUEST_GET_FIRST;
        pVarClient->rr.requestVal = query->type;
        pVarClient->rr.variableInfo.instanceID = query->instanceID;
        pVarClient->rr.variableInfo.flags = query->flags;
        memcpy( &pVarClient->rr.variableInfo.tagspec,
                &query->tagspec,
                MAX_TAGSPEC_LEN );

        if ( query->match != NULL )
        {
            len = strlen(query->match);
            if( ( len > 0 ) && ( len < pVarClient->workbufsize ) )
            {
                /* copy the search string into the working buffer */
                p = &pVarClient->workbuf;
                memcpy( p, query->match, len );

                /* NUL terminate the string */
                p[len] = 0;

                /* set the length of the data in the working buffer */
                pVarClient->rr.len = len+1;
            }
        }

        /* send the request to the server */
        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            query->context = pVarClient->rr.responseVal;
            if( query->context > 0 )
            {
                /* get the name of the variable we found */
                memcpy( query->name,
                        pVarClient->rr.variableInfo.name,
                        MAX_NAME_LEN+1 );

                /* get the handle of the variable we found */
                query->hVar = pVarClient->rr.variableInfo.hVar;
            }
            else
            {
                /* nothing found which matches the query */
                result = ENOENT;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  getNext                                                                   */
/*
    Continue a variable query

    The getNext function continues a variable search and tries to get
    the next result in the set of variable which match the initial variable
    query defined when calling VAR_GetFirst

    @param[in]
        pVarClient
            pointer to the varserver client

    @param[in]
        query
            pointer to the query to be made.  This must be the same
            object that was passed to VAR_GetFirst as it refers to the
            query context established when the search was initiated.

    @param[in]
        obj
            pointer to the found variable information

    @retval EOK - a match was found
    @retval EINVAL - invalid arguments
    @retval ENOENT - no matching variable was found. Search is terminated.

==============================================================================*/
static int getNext( VarClient *pVarClient,
                    VarQuery *query,
                    VarObject *obj )
{
    int result = EINVAL;
    char *p;
    size_t len;

    if( ( pVarClient != NULL ) &&
        ( query != NULL ) &&
        ( obj != NULL ) )
    {
        pVarClient->rr.requestType = VARREQUEST_GET_NEXT;
        pVarClient->rr.requestVal = query->context;
        pVarClient->rr.len = 0;

        /* send the request to the server */
        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            query->context = pVarClient->rr.responseVal;
            if( query->context > 0 )
            {
                /* get the name of the variable we found */
                memcpy( query->name,
                        pVarClient->rr.variableInfo.name,
                        MAX_NAME_LEN+1 );

                /* get the handle of the variable we found */
                query->hVar = pVarClient->rr.variableInfo.hVar;
            }
            else
            {
                /* nothing found which matches the query */
                result = ENOENT;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  notify                                                                    */
/*!
    Register a notification for a specific variable

    The notify function requests a notification for an action
    on the specified variable.

    @param[in]
        pVarClient
            pointer to the varserver client

    @param[in]
        hVar
            handle to the variable to be notified on

    @param[in]
        notification
            the type of notification requested


    @retval EOK - the notification request was registered successfully
    @retval EINVAL - invalid arguments

==============================================================================*/
static int notify( VarClient *pVarClient,
                   VAR_HANDLE hVar,
                   NotificationType notificationType )
{
    int result = EINVAL;

    if( pVarClient != NULL )
    {
        pVarClient->rr.requestType = VARREQUEST_NOTIFY;
        pVarClient->rr.variableInfo.hVar = hVar;
        pVarClient->rr.variableInfo.notificationType = notificationType;
        pVarClient->rr.len = 0;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
    }

    return result;
}


/*============================================================================*/
/*  print                                                                     */
/*!
    Print a variable value to the specified output stream

    The print function prints out the value of the variable
    specified by hVar to the output stream specified by fp.

    @param[in]
        pVarClient
            pointer to the varserver client

    @param[in]
        hVar
            handle to the variable to be printed

    @param[in]
        fd
            specifies the output file descriptor to print to

    @retval EOK - the variable was printed ok
    @retval ENOTSUP - the variable data type is not supported for printing
    @retval EINVAL - invalid arguments

==============================================================================*/
static int print( VarClient *pVarClient,
                  VAR_HANDLE hVar,
                  int fd )
{
    int result = EINVAL;
    pid_t responderPID;
    int sock;

    if( pVarClient != NULL )
    {
        pVarClient->rr.requestType = VARREQUEST_PRINT;
        pVarClient->rr.variableInfo.hVar = hVar;
        pVarClient->rr.len = 0;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( pVarClient->rr.responseVal == ESTRPIPE)
        {
            /* get the PID of the client doing the printing */
            responderPID = (pid_t)(pVarClient->rr.peer_pid);

            /* send the file descriptor to the responder */
            result = VARPRINT_SendFileDescriptor( responderPID, fd );

            if( result == EOK )
            {
                /* block client until printing is complete */
                pVarClient->blocked = 1;
                do
                {
                    result = sem_wait( &pVarClient->sem );
                    if ( result == -1 )
                    {
                        result = errno;
                    }
                } while ( result != EOK );
                pVarClient->blocked = 0;
            }
        }
        else
        {
            result = var_PrintValue( fd,
                                     &pVarClient->rr.variableInfo,
                                     &pVarClient->workbuf );
        }
    }

    return result;
}

/*============================================================================*/
/*  openPrintSession                                                          */
/*!
    Open a new print session

    The openPrintSession creates a new print session,
    creating a link to the requesting client's output stream
    and returning a handle to the variable that should be output.

    @param[in]
        pVarClient
            pointer to the varserver client

    @param[in]
        id
            transaction identifier for the print session

    @param[in]
        hVar
            pointer to the location to store the handle of the variable
            to be output

    @param[in]
        fd
            pointer to the location to store the fd for the output stream

    @retval EOK - the print session was successfully created
    @retval EINVAL - invalid arguments

==============================================================================*/
static int openPrintSession( VarClient *pVarClient,
                             uint32_t id,
                             VAR_HANDLE *hVar,
                             int *fd )
{
    int result = EINVAL;
    int sock;
    pid_t pid;

    if( ( pVarClient != NULL ) &&
        ( hVar != NULL ) &&
        ( fd != NULL ) )
    {
        /* set up a socket to get the file descriptor to print to */
        pid = pVarClient->rr.client_pid;
        result = VARPRINT_SetupListener( pid, &sock );
        if( result == EOK )
        {
            pVarClient->rr.requestType = VARREQUEST_OPEN_PRINT_SESSION;
            pVarClient->rr.requestVal = id;
            pVarClient->rr.len = 0;

            result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
            if( result == EOK )
            {
                /* get a handle to the variable we are printing */
                *hVar = pVarClient->rr.variableInfo.hVar;

                /* get the file descriptor we are printing to */
                result = VARPRINT_GetFileDescriptor( pVarClient->rr.peer_pid,
                                                     sock,
                                                     fd );
            }

            /* shut down the listener */
            VARPRINT_ShutdownListener( pid, sock );
        }
    }

    return result;
}

/*============================================================================*/
/*  closePrintSession                                                         */
/*!
    Conclude a print session

    The closePrintSession terminates an active print session
    and unblocks the requesting client.

    @param[in]
        pVarClient
            pointer to the varserver client

    @param[in]
        id
            transaction identifier for the print session

    @param[in]
        fd
            file descriptor for the print session output stream

    @retval EOK - the print session was successfully completed
    @retval EINVAL - invalid arguments

==============================================================================*/
static int closePrintSession( VarClient *pVarClient,
                              uint32_t id,
                              int fd )
{
    int result = EINVAL;

    if( pVarClient != NULL )
    {
        pVarClient->rr.requestType = VARREQUEST_CLOSE_PRINT_SESSION;
        pVarClient->rr.requestVal = id;
        pVarClient->rr.len = 0;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            close( fd );
        }
    }

    return result;
}

/*============================================================================*/
/*  var_PrintValue                                                            */
/*!
    Print a variable value to the specified output stream

    The var_PrintValue function prints out the value specified in
    the VarInfo structure.  It uses the format specifier associated
    with the variable (if it exists), or a generic %f, %ul, %d etc
    if it does not.

    @param[in]
        fd
            file descriptor to print to

    @param[in]
        pInfo
            pointer to the VarInfo object that contains the variable
            value to be printed

    @param[in]
        workbuf
            pointer to the working buffer where string to be
            printed is located.  Only applicable if the variable
            type is VARTYPE_STR

    @retval EOK - the variable was printed ok
    @retval ENOTSUP - the variable data type is not supported for printing
    @retval EINVAL - invalid arguments

==============================================================================*/
static int var_PrintValue( int fd, VarInfo *pInfo, char *workbuf )
{
    char *fmt;
    int result = EINVAL;

    if( ( fd >= 0 ) &&
        ( pInfo != NULL ) &&
        ( workbuf != NULL ) )
    {
        switch( pInfo->var.type )
        {
            case VARTYPE_FLOAT:
                fmt = ( pInfo->formatspec[0] == 0 ) ? "%f"
                                                    : pInfo->formatspec;
                dprintf(fd, fmt, pInfo->var.val.f );
                result = EOK;
                break;

            case VARTYPE_BLOB:
                dprintf(fd, "%s len=%ld>", "<object:", pInfo->var.len);
                result = EOK;
                break;

            case VARTYPE_STR:
                /* string variable values are transferred via the workbuf */
                fmt = ( pInfo->formatspec[0] == 0 ) ? "%s"
                                                    : pInfo->formatspec;
                dprintf(fd, fmt, workbuf );
                result = EOK;
                break;

            case VARTYPE_UINT16:
                fmt = ( pInfo->formatspec[0] == 0 ) ? "%u"
                                                    : pInfo->formatspec;
                dprintf(fd, fmt, pInfo->var.val.ui );
                result = EOK;
                break;

            case VARTYPE_INT16:
                fmt = ( pInfo->formatspec[0] == 0 ) ? "%d"
                                                    : pInfo->formatspec;
                dprintf(fd, fmt, pInfo->var.val.i );
                result = EOK;
                break;

            case VARTYPE_UINT32:
                fmt = ( pInfo->formatspec[0] == 0 ) ? "%lu"
                                                    : pInfo->formatspec;
                dprintf(fd, fmt, pInfo->var.val.ul );
                result = EOK;
                break;

            case VARTYPE_INT32:
                fmt = ( pInfo->formatspec[0] == 0 ) ? "%d"
                                                    : pInfo->formatspec;
                dprintf(fd, fmt, pInfo->var.val.l );
                result = EOK;
                break;

            case VARTYPE_UINT64:
                fmt = ( pInfo->formatspec[0] == 0 ) ? "%llu"
                                                    : pInfo->formatspec;
                dprintf(fd, fmt, pInfo->var.val.ull );
                result = EOK;
                break;

            case VARTYPE_INT64:
                fmt = ( pInfo->formatspec[0] == 0 ) ? "%lld"
                                                    : pInfo->formatspec;
                dprintf(fd, fmt, pInfo->var.val.ll );
                result = EOK;
                break;

            default:
                result = ENOTSUP;
                break;
        }
    }

    return result;
}



/*============================================================================*/
/*  ClientRequest                                                             */
/*!
    Send a request from the client to the server

    The ClientRequest function is used to send a client request from a
    client to the Variable Server.

    This is a blocking call.  The client will wait until explicitly
    released by the server.  If the server dies, the client will hang!

    @param[in]
        pVarClient
            pointer to the VarClient object belonging to the client

    @param[in]
        request
            specifies the request to be sent from the client to the server

    @retval EOK - the client request was handled successfully by the server
    @retval EINVAL - an invalid client was specified
    @retval other - error code returned by sigqueue, or sem_wait

==============================================================================*/
static int ClientRequest( VarClient *pVarClient, int request )
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
            printf("CLIENT: Sending client request (%d)\n", request );
        }

        /* get the socket descriptor */
        sd = pVarClient->sd;

        /* send request */
        //printf("CLIENT: write requestresponse: len = %ld\n", pVarClient->rr.len );
        rc = writesd( sd, (char *)&pVarClient->rr, sizeof( RequestResponse ) );
        if ( rc == EOK )
        {
            len = pVarClient->rr.len;
            if ( ( len > 0 ) &&
                 ( pVarClient->rr.requestType != VARREQUEST_OPEN ) )
            {
                printf("CLIENT: write client variable request: len = %ld\n", len);
                /* write data from the working buffer */
                rc = writesd( sd, (char *)&pVarClient->workbuf, len );

                pVarClient->rr.len = 0;
            }

            pVarClient->blocked = 1;

            //printf("CLIENT: read requestresponse\n");
            rc = readsd( sd,
                         (char *)&pVarClient->rr,
                         sizeof( RequestResponse ) );
            if ( rc == EOK )
            {
                len = pVarClient->rr.len;
                //printf("CLIENT: pVarClient->rr.len = %ld\n", pVarClient->rr.len );
                if ( len > 0 )
                {
                    printf("CLIENT: Read variable response: n=%ld\n",len);
                    /* read data into the working buffer */
                    rc = readsd( sd,
                                 (char *)&pVarClient->workbuf,
                                len );
                }
            }

            pVarClient->blocked = 0;
        }

        result = rc;
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
//            printf("CLIENT: write: n = %ld\n", n);
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

    if ( ( p != NULL ) && ( len > 0 ) )
    {
        do
        {
            n = read( sd,
                    &p[rcvd],
                    remaining );
//            printf("CLIENT: read: n = %ld\n", n);
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
/*  ClientCleanup                                                             */
/*!
    Clean up the Variable Client when it is no longer needed

    The ClientCleanup function cleans up the Variable Client when it is
    no longer needed.  The client semaphore is deleted, and the
    shared memory VarClient object is unmapped and unlinked.
    The Variable Server's ServerInfo shared memory object is also unmapped.

    @retval EOK the client was successfully shut down
    @retval EINVAL an invalid variable client was specified

==============================================================================*/
static int ClientCleanup( VarClient *pVarClient )
{
    char clientname[BUFSIZ];
    int fd;
    int res;
    int result = EINVAL;

    if( pVarClient != NULL )
    {
        if ( pVarClient->sd > 0 )
        {
            close( pVarClient->sd );
        }

        free( pVarClient );
    }

    return result;
}

/*! @}
 * end of sockapi group */
