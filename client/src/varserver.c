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
 * @defgroup varserver_api varserver_api
 * @brief RealTime In-Memory Publish/Subscribe Key/Value store API
 * @{
 */

/*============================================================================*/
/*!
@file varserver.c

    Variable Server API

    The Variable Server API is the Application Programming Interface to
    the real time in-memory pub/sub key/value store,  All clients will
    use the varserver API to interface with the variable server,  The
    varserver library abstracts all the complexity of interfacing
    with the variable server.

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
#include <sys/signalfd.h>
#include <fcntl.h>
#include <mqueue.h>
#include <errno.h>
#include <semaphore.h>
#include <string.h>
#include <varserver/varobject.h>
#include <varserver/varclient.h>
#include <varserver/varserver.h>
#include <varserver/varprint.h>
#include <varserver/var.h>

/*==============================================================================
        Private function declarations
==============================================================================*/

static int ClientRequest( VarClient *pVarClient, int signal );
static VarClient *NewClient( size_t workbufsize );
static int ClientCleanup( VarClient *pVarClient );
static int DeleteClientSemaphore( VarClient *pVarClient );
static int NewClientSemaphore( VarClient *pVarClient );
static int InitServerInfo( VarClient *pVarClient );
static VarClient *ValidateHandle( VARSERVER_HANDLE hVarServer );
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

static void DeleteClientQueue( VarClient *pVarClient );

/*==============================================================================
        File scoped variables
==============================================================================*/
static const char *flagNames[] =
{
    "none",
    "volatile",
    "readonly",
    "hidden",
    "dirty",
    NULL
};

static const char *typeNames[] =
{
    "invalid",
    "uint16",
    "int16",
    "uint32",
    "int32",
    "uint64",
    "int64",
    "float",
    "str",
    "blob",
    NULL
};

/*==============================================================================
        Function definitions
==============================================================================*/

void __attribute__ ((constructor)) initLibrary(void) {
 //
 // Function that is called when the library is loaded
 //
}
void __attribute__ ((destructor)) cleanUpLibrary(void) {
 //
 // Function that is called when the library is »closed«.
 //
}

/*============================================================================*/
/*  VARSERVER_Open                                                            */
/*!
    Open a connection to the variable server

    The VARSERVER_Open function is used by the variable server clients
    to connect to the variable server and obtain a VARSERVER_HANDLE to
    use for subsequent communication with the server.

    @retval a handle to the variable server
    @retval NULL if the variable server could not be opened

==============================================================================*/
VARSERVER_HANDLE VARSERVER_Open( void )
{
    return VARSERVER_OpenExt( VARSERVER_DEFAULT_WORKBUF_SIZE );
}

/*============================================================================*/
/*  VARSERVER_OpenExt                                                         */
/*!
    Open a connection to the variable server

    The VARSERVER_OpenExt function is used by the variable server clients
    to connect to the variable server and obtain a VARSERVER_HANDLE to
    use for subsequent communication with the server.  It allows
    a custom client-server working buffer size to be specified.

    @param[in]
        workbufsize
            specifies the size of the client-server working buffer

    @retval a handle to the variable server
    @retval NULL if the variable server could not be opened

==============================================================================*/
VARSERVER_HANDLE VARSERVER_OpenExt( size_t workbufsize )
{
    int i;
    int result = EINVAL;
    VarClient *pTempVarClient = NULL;
    VarClient *pVarClient = NULL;
    sigset_t mask;
    static ServerInfo *pServerInfo = NULL;

    /* create a new client instance */
    pTempVarClient = NewClient( workbufsize );
    if( pTempVarClient != NULL )
    {
        sigemptyset(&pTempVarClient->mask);
        sigaddset(&pTempVarClient->mask, SIG_CLIENT_RESPONSE );
        sigprocmask(SIG_BLOCK, &pTempVarClient->mask, NULL );

        /* initialize the server information object */
        if( InitServerInfo(pTempVarClient) == EOK )
        {
            /* tell the variable server about us */
            if( ClientRequest( pTempVarClient, SIG_NEWCLIENT ) == EOK )
            {
                if( pTempVarClient->debug >= LOG_DEBUG )
                {
                    printf( "CLIENT: identifier is %d\n",
                            pTempVarClient->clientid );
                }

                if ( pTempVarClient->clientid != 0 )
                {
                    pVarClient = pTempVarClient;
                }
            }
        }
    }

    if( pVarClient == NULL )
    {
        ClientCleanup( pTempVarClient );
    }

    return pVarClient;
}

/*============================================================================*/
/*  VARSERVER_CreateClientQueue                                               */
/*!
    Create a new client notification queue

    The VARSERVER_CreateClientQueue function creates a new client notification
    queue to receive variable modified notifications from the server.

    @param[in]
        hVarServer
            handle to the variable server

    @param[in]
        queuelen
            max number of queued messages.  -1 = use default

    @param[in]
        msgsize
            max number of queueud messages. -1 = use default;

    @retval EOK the new client queue was successfully created
    @retval EINVAL an invalid variable client was specified
    @retval other error from mq_open()

==============================================================================*/
int VARSERVER_CreateClientQueue( VARSERVER_HANDLE hVarServer,
                                 long queuelen,
                                 long msgsize )
{
    int result = EINVAL;
    VarClient *pVarClient = ValidateHandle( hVarServer );
    char clientname[BUFSIZ];
    struct mq_attr attr;

    if( pVarClient != NULL )
    {
        /* build the varclient identifier */
        sprintf(clientname, "/varclient_%d", pVarClient->client_pid);

        attr.mq_flags = 0;
        attr.mq_maxmsg = queuelen == -1 ? VARSERVER_MAX_NOTIFICATION_MSG_COUNT
                                        : queuelen;
        attr.mq_msgsize = msgsize == -1 ? VARSERVER_MAX_NOTIFICATION_MSG_SIZE
                                        : msgsize;
        attr.mq_curmsgs = 0;

        pVarClient->notificationQ = mq_open( clientname,
                                             O_RDONLY | O_CREAT | O_NONBLOCK,
                                             0644,
                                             &attr );

        if ( pVarClient->notificationQ == -1 )
        {
            printf( "Failed to create queue %s : %s\n",
                    clientname,
                    strerror(errno));
            result = errno;
        }
        else
        {
            result = EOK;
        }
    }

    return result;
}

/*============================================================================*/
/*  VARSERVER_Close                                                           */
/*!
    Close the connection to the variable server

    The VARSERVER_Close function is used by the variable server clients
    to disconnect from the variable server and clean up all resources
    used for the connection.

    @param[in]
        hVarServer
            handle to the Variable Server

    @retval EOK - the connection was successfully closed
    @retval EINVAL - an invalid variable server handle was specified

==============================================================================*/
int VARSERVER_Close( VARSERVER_HANDLE hVarServer )
{
    int result = EINVAL;
    VarClient *pVarClient = ValidateHandle( hVarServer );
    if( pVarClient != NULL )
    {
        pVarClient->requestType = VARREQUEST_CLOSE;
        ClientRequest( pVarClient, SIG_CLIENT_REQUEST );

        /* clean up the Var client */
        ClientCleanup( pVarClient );

        /* indicate success */
        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  VARSERVER_GetWorkingBuffer                                                */
/*!
    Get a pointer to the working buffer for the varserver

    The VARSERVER_GetWorkingBuffer gets a pointer to the client-server
    working buffer and also gets its length.

    @param[in]
        hVarServer
            handle to the Variable Server

    @param[out]
        pBuf
            pointer to the location to store the pointer to the
            working buffer

    @param[out]
        pLen
            pointer to the location to store the working buffer length

    @retval EOK - the working buffer pointer was successfully retrieved
    @retval EINVAL - invalid arguments

==============================================================================*/
int VARSERVER_GetWorkingBuffer( VARSERVER_HANDLE hVarServer,
                                char **pBuf,
                                size_t *pLen )
{
    int result = EINVAL;
    VarClient *pVarClient = ValidateHandle( hVarServer );

    if( ( pVarClient != NULL ) &&
        ( pBuf != NULL ) &&
        ( pLen != NULL ))
    {
        *pBuf = &pVarClient->workbuf;
        *pLen = pVarClient->workbufsize;

        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  VARSERVER_Debug                                                           */
/*!
    Set the Var Server debugging verbosity level

    The VARSERVER_Debug function sets the verbosity level of the
    variable server client interactions. It returns the previous
    debug setting to support temporarily bumping up the debug
    verbosity and then restoring it to its previous level.

    @param[in]
        hVarServer
            handle to the Variable Server

    @param[out]
        debug
            debug verbosity level 0=no debug.  Higher number=more verbosity

    @return the previous debug verbosity level

==============================================================================*/
int VARSERVER_Debug( VARSERVER_HANDLE hVarServer, int debug )
{
    VarClient *pVarClient = ValidateHandle( hVarServer );
    int olddebug = 0;

    if( pVarClient != NULL )
    {
        olddebug = pVarClient->debug;
        pVarClient->debug = debug;
    }

    return olddebug;
}

/*============================================================================*/
/*  VARSERVER_CreateVar                                                       */
/*!
    Create a new variable

    The VARSERVER_CreateVar function sends a request to the variable
    server to create a new variable.

    @param[in]
        hVarServer
            handle to the Variable Server

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
int VARSERVER_CreateVar( VARSERVER_HANDLE hVarServer,
                         VarInfo *pVarInfo )
{
    int result = EINVAL;
    int rc;
    VarClient *pVarClient = ValidateHandle( hVarServer );

    if( ( pVarClient != NULL ) &&
        ( pVarInfo != NULL ) )
    {
        /* copy the variable information */
        memcpy( &pVarClient->variableInfo, pVarInfo, sizeof( VarInfo ) );

        pVarClient->requestType = VARREQUEST_NEW;
        rc = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( rc == EOK )
        {
            result = pVarClient->responseVal;
        }
        else
        {
            result = rc;
        }

    }

    return result;
}

/*============================================================================*/
/*  VARSERVER_Test                                                            */
/*!
    Test the connection to the variable server

    The VARSERVER_Test function is used by the variable server clients
    to test the connection to the variable server and exercise the
    API.

    @param[in]
        hVarServer
            handle to the Variable Server

    @retval EOK - the connection was successfully closed
    @retval EINVAL - an invalid variable server handle was specified

==============================================================================*/
int VARSERVER_Test( VARSERVER_HANDLE hVarServer )
{
    int result = EINVAL;
    int i;
    VarClient *pVarClient = ValidateHandle( hVarServer );
    if( pVarClient != NULL )
    {
        for(i=0;i<100;i++)
        {
            pVarClient->requestVal = i;
            pVarClient->requestType = VARREQUEST_ECHO;
            ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
            if( pVarClient->debug >= LOG_DEBUG )
            {
                printf("Client %d sent %d and received %d\n",
                    pVarClient->clientid,
                    pVarClient->requestVal,
                    pVarClient->responseVal);
            }
        }

        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  VARSERVER_WaitSignal                                                      */
/*!
    Wait for a VARSERVER signal

    The VARSERVER_WaitSignal function sets up a signal mask for the following
    varserver signals:

        - SIG_VAR_MODIFIED
        - SIG_VAR_QUEUE_MODIFIED
        - SIG_VAR_CALC
        - SIG_VAR_PRINT
        - SIG_VAR_VALIDATE

    It then waits until one of these signals occurs.

    @param[in]
        sigval
            pointer to an integer to store the signal sival_int

    @return the signal which occurred

==============================================================================*/
int VARSERVER_WaitSignal( int *sigval )
{
    sigset_t mask;
    siginfo_t info;
    int sig;

    /* initialize empty signal set */
    sigemptyset( &mask );

    /* modified notification */
    sigaddset( &mask, SIG_VAR_MODIFIED );
    /* queue modified notification */
    sigaddset( &mask, SIG_VAR_QUEUE_MODIFIED );
    /* calc notification */
    sigaddset( &mask, SIG_VAR_CALC );
    /* validate notification */
    sigaddset( &mask, SIG_VAR_PRINT );
    /* print notification */
    sigaddset( &mask, SIG_VAR_VALIDATE );
    /* timer notification */
    sigaddset( &mask, SIG_VAR_TIMER );

    /* block on these signals */
    sigprocmask( SIG_BLOCK, &mask, NULL );

    /* wait for a signal */
    sig = sigwaitinfo( &mask, &info );

    if( sigval != NULL )
    {
        *sigval = info.si_value.sival_int;
    }

    return sig;
}

/*============================================================================*/
/*  VARSERVER_Signalfd                                                        */
/*!
    Create a signal file descriptor

    The VARSERVER_Signalfd function sets up a file descriptor which can
    be read to retrieve VarServer signals.  The signals which can be trapped
    are:

        - SIG_VAR_MODIFIED
        - SIG_VAR_QUEUE_MODIFIED
        - SIG_VAR_CALC
        - SIG_VAR_PRINT
        - SIG_VAR_VALIDATE
        - SIG_VAR_TIMER

    @retval the signal file descriptor
    @retval -1 an error occurred (see errno)

==============================================================================*/
int VARSERVER_Signalfd( void )
{
    sigset_t mask;
    siginfo_t info;
    int sig;

    /* initialize empty signal set */
    sigemptyset( &mask );

    /* modified notification */
    sigaddset( &mask, SIG_VAR_MODIFIED );
    /* queue modified notification */
    sigaddset( &mask, SIG_VAR_QUEUE_MODIFIED );
    /* calc notification */
    sigaddset( &mask, SIG_VAR_CALC );
    /* validate notification */
    sigaddset( &mask, SIG_VAR_PRINT );
    /* print notification */
    sigaddset( &mask, SIG_VAR_VALIDATE );
    /* timer notification */
    sigaddset( &mask, SIG_VAR_TIMER );

    /* block on these signals */
    sigprocmask( SIG_BLOCK, &mask, NULL );

    /* return the file descriptor to read to get signals */
    return signalfd( -1, &mask, 0 );
}

/*============================================================================*/
/*  VARSERVER_WaitSignalfd                                                    */
/*!
    Wait for a VarServer signal

    The VARSERVER_WaitSignalfd function waits for a signal using the
    file descriptor provided by VARSERVER_Signalfd.

    Signals which may be awaited are:

        - SIG_VAR_MODIFIED
        - SIG_VAR_QUEUE_MODIFIED
        - SIG_VAR_CALC
        - SIG_VAR_PRINT
        - SIG_VAR_VALIDATE
        - SIG_VAR_TIMER

    The sigval field contains the payload value associated with the signal

    @param[in]
        fd
            file descriptor provided by VARSERVER_Signalfd

    @param[in]
        sigval
            pointer to a location to store the associated signal value

    @retval the received signal
    @retval -1 an error occurred (see errno)

==============================================================================*/
int VARSERVER_WaitSignalfd( int fd, int32_t *sigval )
{
    struct signalfd_siginfo info;
    int n;
    int sig = -1;

    n = read( fd, &info, sizeof(struct signalfd_siginfo));
    if ( n == sizeof( struct signalfd_siginfo ))
    {
        sig = info.ssi_signo;
        if ( sigval != NULL )
        {
            *sigval = info.ssi_int;
        }
    }

    return sig;
}
/*============================================================================*/
/*  VARSERVER_SigMask                                                         */
/*!
    Get the VARSERVER signal mask

    The VARSERVER_SigMask function sets up a signal mask for the following
    varserver signals:

        - SIG_VAR_MODIFIED
        - SIG_VAR_QUEUE_MODIFIED
        - SIG_VAR_CALC
        - SIG_VAR_PRINT
        - SIG_VAR_VALIDATE
        - SIG_VAR_TIMER

    @return the signal mask

==============================================================================*/
sigset_t VARSERVER_SigMask( void )
{
    sigset_t mask;

    /* initialize empty signal set */
    sigemptyset( &mask );

    /* modified notification */
    sigaddset( &mask, SIG_VAR_MODIFIED );
    /* queue modified notification */
    sigaddset( &mask, SIG_VAR_QUEUE_MODIFIED );
    /* calc notification */
    sigaddset( &mask, SIG_VAR_CALC );
    /* validate notification */
    sigaddset( &mask, SIG_VAR_PRINT );
    /* print notification */
    sigaddset( &mask, SIG_VAR_VALIDATE );
    /* timer notification */
    sigaddset( &mask, SIG_VAR_TIMER );

    /* block on these signals */
    sigprocmask( SIG_BLOCK, &mask, NULL );

    return mask;
}

/*============================================================================*/
/*  VARSERVER_ParsePermissionSpec                                             */
/*!
    Convert a comma separated list of UIDs to a UID array

    The VARSERVER_ParsePermissionSpec function splits the specified
    permission specifier string on commas, and converts the
    permission UID strings into numeric UIDs for the permission UID
    array.

    @param[in]
        permissionSpec
            Comma separated UID permission list

    @param[out]
        permissions
            pointer to the output array of permission UIDs

    @param[in]
        len
            size of the output permission UID array

    @retval EOK - the flags parsing was successful
    @retval EINVAL - invalid arguments
    @retval E2BIG - the specified permission list is too big

==============================================================================*/
int VARSERVER_ParsePermissionSpec( char *permissionSpec,
                                   uint16_t *permissions,
                                   size_t len )
{
    int i = 0;
    int result = EINVAL;
    char *permission;
    char buf[ MAX_PERMISSIONSPEC_LEN + 1 ];
    size_t permissionSpecLength;
    char *r;

    if( ( permissionSpec != NULL ) &&
        ( permissions != NULL ) &&
        ( len > 0 ) &&
        ( len <= MAX_UIDS ) )
    {
        /* initialize the result */
        memset( permissions, 0, sizeof( uint16_t) * len );
        result = EOK;

        /* get the length of the permission specifier string */
        permissionSpecLength = strlen( permissionSpec );
        if( permissionSpecLength <= MAX_PERMISSIONSPEC_LEN )
        {
            /* assume success until we exceed our output buffer size */
            result = EOK;

            /* create a working copy of the permission specifier string */
            memcpy( buf, permissionSpec, permissionSpecLength );
            buf[ permissionSpecLength ] = 0;

            /* split on commas */
            r = buf;
            while( ( permission = strtok_r( r, ",", &r ) ) )
            {
                if( ( i < len ) &&
                    ( i < MAX_UIDS ) )
                {
                    permissions[i++] = atoi( permission );
                }
                else
                {
                    result = E2BIG;
                    break;
                }
            }
        }
        else
        {
            /* permission specifier string is too long */
            result = E2BIG;
        }
    }

    return result;
}

/*============================================================================*/
/*  VARSERVER_TypeNameToType                                                  */
/*!
    Convert a type name to its corresponding type

    The VARSERVER_TypeNameToType function converts the specified
    type name into its corresponding enumerated type.

    @param[in]
        typeName
            Name of the type to look up

    @param[out]
        type
            pointer to the output type variable

    @retval EOK - the type name lookup was successful
    @retval EINVAL - invalid arguments
    @retval ENOENT - the specified type name does not exist

==============================================================================*/
int VARSERVER_TypeNameToType( char *typeName, VarType *type )
{
    int i = 0;
    int result = EINVAL;

    if( ( typeName != NULL ) &&
        ( type != NULL ) )
    {
        /* populate invalid type in case we don't find a match */
        *type = VARTYPE_INVALID;
        result = ENOENT;

        while( typeNames[i] != NULL )
        {
            if( strcasecmp( typeNames[i], typeName ) == 0 )
            {
                *type = i;
                result = EOK;
                break;
            }

            /* select the next type name */
            i++;
        }
    }

    return result;
}

/*============================================================================*/
/*  VARSERVER_TypeToTypeName                                                  */
/*!
    Get the name of a variable type

    The VARSERVER_TypeToTypeName function converts the specified
    enumerated type into its corresponding type name.

    @param[in]
        type
            Enumerated type to convert

    @param[out]
        typeName
            pointer to an output buffer to copy the type name

    @param[in]
        len
            length of the output buffer to receive the type name

    @retval EOK - the type name lookup was successful
    @retval EINVAL - invalid arguments
    @retval ENOENT - the specified type name does not exist
    @retval E2BIG - the output buffer is not big enough for the type name

==============================================================================*/
int VARSERVER_TypeToTypeName( VarType type, char *typeName, size_t len )
{
    int result = EINVAL;
    size_t typeNameLength;

    if( typeName != NULL )
    {
        if( type < VARTYPE_END_MARKER )
        {
            typeNameLength = strlen( typeNames[type] );
            if( typeNameLength < len )
            {
                strcpy( typeName, typeNames[type] );
                result = EOK;
            }
            else
            {
                result = E2BIG;
            }
        }
        else
        {
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  VARSERVER_StrToFlags                                                      */
/*!
    Convert a comma separated list of flag names to a bitmap

    The VARSERVER_StrToFlags function splits the specified flags string
    on commas, and iterates through the flag names looking for a
    match and setting the appropriate bits in the flags bitmap

    @param[in]
        flagsString
            Comma separated flag name string

    @param[out]
        flags
            pointer to a location to store the 32-bit flag bitmask

    @retval EOK - the flags parsing was successful
    @retval EINVAL - invalid arguments
    @retval E2BIG - the specified flagsString is too big
    @retval ENOENT - one or more flags were not valid

==============================================================================*/
int VARSERVER_StrToFlags( char *flagsString,
                          uint32_t *flags )
{
    int i = 0;
    char flagBuf[MAX_FLAGSPEC_LEN+1];
    int result = EINVAL;
    size_t len;
    char *flag;
    char *r;

    if( ( flagsString != NULL ) &&
        ( flags != NULL ) )
    {
        /* initialize the result */
        *flags = 0;
        result = EOK;

        /* copy the flag specifier string into local storage */
        len = strlen( flagsString );
        if( len <= MAX_FLAGSPEC_LEN )
        {
            /* copy the specified flags string into a working buffer */
            memcpy( flagBuf, flagsString, len );
            flagBuf[len] = 0;

            /* split on commas */
            r = flagBuf;
            while( ( flag = strtok_r( r, ",", &r ) ) )
            {
                /* iterate through the flag names looking for a match */
                i = 1;
                while( flagNames[i] != NULL )
                {
                    /* check for a case insensitive flag name match */
                    if( strcasecmp( flagNames[i], flag ) == 0 )
                    {
                        /* set the corresponding bit in the flags bitmap */
                        *flags |= ( 1 << ( i-1 ) );
                        break;
                    }

                    /* move to the next flag name */
                    i++;
                }

                if( flagNames[i] == NULL )
                {
                    result = ENOENT;
                }
            }
        }
        else
        {
            result = E2BIG;
        }
    }

    return result;
}

/*============================================================================*/
/*  VARSERVER_FlagsToStr                                                      */
/*!
    Convert a flags bitmap into a comma separated list of flag names

    The VARSERVER_FlagsToStr function generates a comma separated
    flag name string from the specified flags bitmap

    @param[in]
        flags
            32-bit flags bitmap

    @param[out]
        flagsString
            pointer to a buffer to store the comma separated flag string

    @param[in]
        len
            size of the buffer to store the comma separated flag string


    @retval EOK - the flags parsing was successful
    @retval EINVAL - invalid arguments
    @retval E2BIG - not enough space in the output buffer for the flag string

==============================================================================*/
int VARSERVER_FlagsToStr( uint32_t flags,
                          char *flagsString,
                          size_t len )
{
    int i=1;
    size_t remaining = len;
    size_t offset = 0;
    int count = 0;
    int result = EINVAL;

    if( ( flagsString != NULL ) &&
        ( len > 0 ) )
    {
        /* indicate success until we know otherwise */
        result = EOK;

        while( flagNames[i] != NULL )
        {
            if( flags & ( 1 << ( i-1 ) ) )
            {
                /* calculate length needed for the flag name */
                len = strlen( flagNames[i] );

                if( count > 0 )
                {
                    /* add one for the comma separator */
                    len++;
                }

                /* check if we have enough space for the flag in the string */
                if( remaining > len )
                {
                    /* check if this is not the first flag */
                    if( count > 0 )
                    {
                        /* prepend a comma */
                        flagsString[offset] = ',';
                        offset++;
                        remaining--;
                    }

                    /* build the flags string */
                    strcpy( &flagsString[offset], flagNames[i] );

                    /* adjust offset and remaining byte count by the
                       length of the flag name */
                    offset += len;
                    remaining -= len;
                }
                else
                {
                    /* we have run out of space for the flags string */
                    result = E2BIG;
                }
            }

            /* move to the next flag */
            i++;
        }
    }

    return result;
}

/*============================================================================*/
/*  VARSERVER_ParseValueString                                                */
/*!
    Parse a value string and assign a value to the VarObject

    The VARSERVER_ParseValueString function parses a value string
    and converts the string into an appropriate value depending on
    the variable type.  The converted value is stored into the
    specfied VarObject.

    @param[out]
        var
            pointer to a VarObject to receive the parsed value

    @param[out]
        valueString
            pointer to a value string to be parsed

    @retval EOK - the value string was successfully parsed and stored
    @retval EINVAL - invalid arguments
    @retval E2BIG - the specified string was too large
    @retval ERANGE - the specified value does not fit in the variable type
    @retval ENOTSUP - unsupported data type

==============================================================================*/
int VARSERVER_ParseValueString( VarObject *var, char *valueString )
{
    int result = EINVAL;
    size_t len;
    int32_t lVal;
    uint32_t ulVal;
    int64_t llVal;
    uint64_t ullVal;
    int16_t iVal;
    uint16_t uiVal;

    int base = 0;

    if( ( var != NULL ) &&
        ( valueString != NULL ) )
    {
        /* assume success until we determine otherwise */
        result = EOK;

        if( ( valueString[0] == '0' ) &&
            ( tolower(valueString[1]) == 'x' ) )
        {
            base = 16;
        }

        switch( var->type )
        {
            case VARTYPE_INT16:
                var->val.i = (int16_t)strtol( valueString, NULL, base );
                break;

            case VARTYPE_UINT16:
                var->val.ui = (uint16_t)strtoul( valueString, NULL, base );
                break;

            case VARTYPE_INT32:
                var->val.l = strtol( valueString, NULL, base );
                break;

            case VARTYPE_UINT32:
                var->val.ul = strtoul( valueString, NULL, base );
                break;

            case VARTYPE_INT64:
                var->val.ll = strtoll( valueString, NULL, base );
                break;

            case VARTYPE_UINT64:
                var->val.ull = strtoull( valueString, NULL, base );
                break;

            case VARTYPE_FLOAT:
                var->val.f = strtof( valueString, NULL );
                break;

            case VARTYPE_STR:
                if( var->val.str != NULL)
                {
                    len = strlen( valueString );
                    if( len < var->len )
                    {
                        strcpy( var->val.str, valueString );
                    }
                    else
                    {
                        result = E2BIG;
                    }
                }
                else
                {
                    result = ENOMEM;
                }

                break;

            case VARTYPE_BLOB:
                if( var->val.blob != NULL)
                {
                    len = strlen( valueString );
                    if( len <= var->len )
                    {
                        memcpy( var->val.blob, valueString, len );
                    }
                    else
                    {
                        result = E2BIG;
                    }
                }
                else
                {
                    result = ENOMEM;
                }

                break;

            default:
                result = ENOTSUP;
                break;
        }
    }

    return result;
}

/*============================================================================*/
/*  VAR_FindByName                                                            */
/*!
    Find a variable given its name

    The VAR_FindByName function requests the handle for the specified
    variable from the variable server.

    @param[in]
        hVarServer
            handle to the Variable Server

    @param[in]
        pName
            pointer to the variable name

    @retval handle of the variable
    @retval VAR_INVALID if the variable cannot be found

==============================================================================*/
VAR_HANDLE VAR_FindByName( VARSERVER_HANDLE hVarServer, char *pName )
{
    VAR_HANDLE hVar = VAR_INVALID;
    VarClient *pVarClient = ValidateHandle( hVarServer );
    int rc;
    size_t len;

    if( ( pVarClient != NULL ) &&
        ( pName != NULL ) )
    {
        len = strlen(pName);
        if( len < MAX_NAME_LEN )
        {
            /* copy the name to the variable info request */
            strcpy(pVarClient->variableInfo.name, pName );
            pVarClient->variableInfo.instanceID = 0;

            /* specify the request type */
            pVarClient->requestType = VARREQUEST_FIND;

            rc = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
            if( rc == EOK )
            {
                hVar = (VAR_HANDLE)pVarClient->responseVal;
            }
        }
    }

    return hVar;
}

/*============================================================================*/
/*  VAR_Get                                                                   */
/*!
    Get a variable value and store it in the specified var object

    The VAR_Get function gets the value of the variable
    specified by hVar and puts it into the specified var object

    @param[in]
        hVarServer
            handle to the variable server

    @param[in]
        hVar
            handle to the variable to be retrieved

    @param[in]
        pVarObject
            specifies the location where the variable value should be stored

    @retval EOK - the variable was retrieved ok
    @retval EINVAL - invalid arguments

==============================================================================*/
int VAR_Get( VARSERVER_HANDLE hVarServer,
             VAR_HANDLE hVar,
             VarObject *pVarObject )
{
    int result = EINVAL;
    VarClient *pVarClient = ValidateHandle( hVarServer );
    int n;

    if( ( pVarClient != NULL ) &&
        ( pVarObject != NULL ) )
    {
        pVarClient->requestType = VARREQUEST_GET;
        pVarClient->variableInfo.hVar = hVar;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            result = var_GetVarObject( pVarClient, pVarObject );
        }
    }

    return result;
}

/*============================================================================*/
/*  VAR_GetStrByName                                                          */
/*!
    Get a string variable value and store it in the specified buffer

    The VAR_GetStrByName function gets the value of the string variable
    specified by hVar and puts it into the specified buffer

    @param[in]
        hVarServer
            handle to the variable server

    @param[in]
        name
            name of the variable to be retrieved

    @param[in,out]
        buf
            pointer to the buffer to store the string

    @param[in]
        len
            length of the provided buffer to store the result

    @retval EOK - the variable was retrieved ok
    @retval EINVAL - invalid arguments
    @retval ENOTSUP - invalid variable type
    @retval ENOENT - variable not found
    @retval other error as returned by VAR_Get

==============================================================================*/
int VAR_GetStrByName( VARSERVER_HANDLE hVarServer,
                      char *name,
                      char *buf,
                      size_t len )
{
    int result = EINVAL;
    VAR_HANDLE hVar;
    VarObject obj;

    if ( ( hVarServer != NULL ) &&
         ( name != NULL ) &&
         ( buf != NULL ) &&
         ( len > 0 ) )
    {
        /* get a handle to the variable */
        hVar = VAR_FindByName( hVarServer, name );
        if ( hVar != VAR_INVALID )
        {
            /* specify the receive buffer and buffer length */
            obj.val.str = buf;
            obj.len = len;

            /* get the (string) value of the variable */
            result = VAR_Get( hVarServer, hVar, &obj );
            if ( result == EOK )
            {
                if( obj.type != VARTYPE_STR )
                {
                    result = ENOTSUP;
                }
            }
        }
        else
        {
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  VAR_GetBlobByName                                                         */
/*!
    Get a blob variable value and store it in the specified buffer

    The VAR_GetBlobByName function gets the value of the blob variable
    specified by hVar and puts it into the specified buffer

    @param[in]
        hVarServer
            handle to the variable server

    @param[in]
        name
            name of the variable to be retrieved

    @param[in,out]
        buf
            pointer to the buffer to store the blob

    @param[in]
        len
            length of the provided buffer to store the result

    @retval EOK - the variable was retrieved ok
    @retval EINVAL - invalid arguments
    @retval ENOTSUP - invalid variable type
    @retval ENOENT - variable not found
    @retval other error as returned by VAR_Get

==============================================================================*/
int VAR_GetBlobByName( VARSERVER_HANDLE hVarServer,
                       char *name,
                       void *buf,
                       size_t len )
{
    int result = EINVAL;
    VAR_HANDLE hVar;
    VarObject obj;

    if ( ( hVarServer != NULL ) &&
         ( name != NULL ) &&
         ( buf != NULL ) &&
         ( len > 0 ) )
    {
        /* get a handle to the variable */
        hVar = VAR_FindByName( hVarServer, name );
        if ( hVar != VAR_INVALID )
        {
            /* specify the receive buffer and buffer length */
            obj.val.str = buf;
            obj.len = len;

            /* get the (blob) value of the variable */
            result = VAR_Get( hVarServer, hVar, &obj );
            if ( result == EOK )
            {
                if( obj.type != VARTYPE_BLOB )
                {
                    result = ENOTSUP;
                }
            }
        }
        else
        {
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  VAR_GetValidationRequest                                                  */
/*!
    Get a information about a validation request

    The VAR_GetValidationRequest function gets the the validation
    request specified by the validation request identifier.
    The returned VarObject will contain the proposed variable
    change requested by the other client.

    @param[in]
        hVarServer
            handle to the variable server

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
int VAR_GetValidationRequest( VARSERVER_HANDLE hVarServer,
                              uint32_t id,
                              VAR_HANDLE *hVar,
                              VarObject *pVarObject )
{
    int result = EINVAL;
    VarClient *pVarClient = ValidateHandle( hVarServer );

    if( ( pVarClient != NULL ) &&
        ( pVarObject != NULL ) &&
        ( hVar != NULL ) )
    {
        pVarClient->requestType = VARREQUEST_GET_VALIDATION_REQUEST;
        pVarClient->requestVal = id;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            result = var_GetVarObject( pVarClient, pVarObject );
            if( result == EOK )
            {
                /* get the handle of the variable to be validated */
                *hVar = pVarClient->variableInfo.hVar;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  VAR_SendValidationResponse                                                */
/*!
    Send a Validation response

    The VAR_SendValidationResponse function sends a validation response
    for the specified validation reqeust.

    @param[in]
        hVarServer
            handle to the variable server

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
int VAR_SendValidationResponse( VARSERVER_HANDLE hVarServer,
                                uint32_t id,
                                int response  )
{
    int result = EINVAL;
    VarClient *pVarClient = ValidateHandle( hVarServer );

    if( pVarClient != NULL )
    {
        pVarClient->requestType = VARREQUEST_SEND_VALIDATION_RESPONSE;
        pVarClient->requestVal = id;
        pVarClient->responseVal = response;

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
        pVarObject->type = pVarClient->variableInfo.var.type;
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
            pVarObject->val = pVarClient->variableInfo.var.val;

            /* get the source object length */
            srclen = pVarClient->variableInfo.var.len;

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
        pVarObject->type = pVarClient->variableInfo.var.type;
        if ( pVarObject->type == VARTYPE_STR )
        {
            /* get the source string */
            pVarClient->variableInfo.var.val.str = &pVarClient->workbuf;
            pSrcString = pVarClient->variableInfo.var.val.str;

            if( pSrcString != NULL )
            {
                /* get the source object length */
                srclen = pVarClient->variableInfo.var.len;

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
        pVarObject->type = pVarClient->variableInfo.var.type;
        if ( pVarObject->type == VARTYPE_BLOB )
        {
            /* get the source blob */
            pVarClient->variableInfo.var.val.blob = &pVarClient->workbuf;
            pSrcBlob = pVarClient->variableInfo.var.val.blob;

            if( pSrcBlob != NULL )
            {
                /* get the source object length */
                srclen = pVarClient->variableInfo.var.len;

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
/*  VAR_GetLength                                                             */
/*!
    Get the length of the specified variable

    The VAR_GetLength function queries the variable server for the
    length of the specified variable.  Typically this is only useful
    for strings and blobs because the lengths of the other data types could
    easily be calculated directly.

    @param[in]
        hVarServer
            handle to the variable server

    @param[in]
        hVar
            handle to the variable to be retrieved

    @param[in]
        len
            specifies the location where the length should be stored

    @retval EOK - the length was retrieved ok
    @retval EINVAL - invalid arguments

==============================================================================*/
int VAR_GetLength( VARSERVER_HANDLE hVarServer,
                   VAR_HANDLE hVar,
                   size_t *len )
{
    int result = EINVAL;
    VarClient *pVarClient = ValidateHandle( hVarServer );

    if( ( pVarClient != NULL ) &&
        ( len != NULL ) )
    {
        pVarClient->requestType = VARREQUEST_LENGTH;
        pVarClient->variableInfo.hVar = hVar;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            *len = pVarClient->variableInfo.var.len;
        }
    }
}

/*============================================================================*/
/*  VAR_GetType                                                               */
/*!
    Get the variable data type

    The VAR_GetType function gets the type of the variable
    specified by hVar and puts it into the specified VarType object

    @param[in]
        hVarServer
            handle to the variable server

    @param[in]
        hVar
            handle to the variable to be retrieved

    @param[out]
        pVarType
            pointer to the VarType object to populate

    @retval EOK - the variable type was retrieved ok
    @retval EINVAL - invalid arguments

==============================================================================*/
int VAR_GetType( VARSERVER_HANDLE hVarServer,
                 VAR_HANDLE hVar,
                 VarType *pVarType )
{
    int result = EINVAL;
    VarClient *pVarClient = ValidateHandle( hVarServer );

    if( ( pVarClient != NULL ) &&
        ( pVarType != NULL ) )
    {
        pVarClient->requestType = VARREQUEST_TYPE;
        pVarClient->variableInfo.hVar = hVar;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            *pVarType = pVarClient->variableInfo.var.type;
        }
        else
        {
            *pVarType = VARTYPE_INVALID;
        }
    }

    return result;
}

/*============================================================================*/
/*  VAR_GetName                                                               */
/*!
    Get the variable name given its handle

    The VAR_GetName function gets the name of the variable
    specified by hVar and puts it into the specified buffer

    @param[in]
        hVarServer
            handle to the variable server

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
int VAR_GetName( VARSERVER_HANDLE hVarServer,
                 VAR_HANDLE hVar,
                 char *buf,
                 size_t buflen )
{
    int result = EINVAL;
    VarClient *pVarClient = ValidateHandle( hVarServer );

    if( ( pVarClient != NULL ) &&
        ( buf != NULL ) &&
        ( buflen > 0 ) &&
        ( hVar != VAR_INVALID ) )
    {
        pVarClient->requestType = VARREQUEST_NAME;
        pVarClient->variableInfo.hVar = hVar;

        /* make a request to the server to get the variable name */
        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            /* check if the specified buffer is large enough for the name */
            if ( buflen >= strlen( pVarClient->variableInfo.name ) )
            {
                /* copy the variable name into the supplied buffer */
                strcpy( buf, pVarClient->variableInfo.name );

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
/*  VAR_SetNameValue                                                          */
/*!
    Set a variable value

    The VAR_SetNameValue function sets the value of the specified variable.
    The value of the variable is specified as a string and is converted
    to the appropriate type by this function.

    @param[in]
        hVarServer
            handle to the variable server

    @param[in]
        name
            pointer to the name of the variable

    @param[in]
        value
            pointer to the value of the variable

    @retval EOK - the variable was set ok
    @retval E2BIG - the variable string is too big
    @retval ERANGE - the variable is out of range for the specified type
    @retval EINVAL - invalid arguments

==============================================================================*/
int VAR_SetNameValue( VARSERVER_HANDLE hVarServer,
                      char *name,
                      char *value )
{
    int result = EINVAL;
    VAR_HANDLE hVar;
    VarType type;

    if( ( hVarServer != NULL ) &&
        ( name != NULL ) &&
        ( value != NULL ) )
    {
        /* get a handle to the variable given its name */
        hVar = VAR_FindByName( hVarServer, name );
        if( hVar != VAR_INVALID )
        {
            /* get the variable type so we can convert the
               string to a VarObject */
            result = VAR_GetType( hVarServer, hVar, &type );
            if( result == EOK )
            {
                /* set the variable value from the string */
                result = VAR_SetStr( hVarServer, hVar, type, value );
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  VAR_SetStr                                                                */
/*!
    Set a variable value

    The VAR_Set function sets the value of the variable
    specified by hVar to the value contained in the string

    @param[in]
        hVarServer
            handle to the variable server

    @param[in]
        hVar
            handle to the variable to be set

    @param[in]
        pVarObject
            pointer to the variable value object to set

    @retval EOK - the variable was set ok
    @retval E2BIG - the variable string is too big
    @retval ERANGE - the variable is out of range for the specified type
    @retval EINVAL - invalid arguments

==============================================================================*/
int VAR_SetStr( VARSERVER_HANDLE hVarServer,
                VAR_HANDLE hVar,
                VarType type,
                char *str )
{
    int result = EINVAL;
    VarObject varObject = {0};

    if( str != NULL )
    {
        /* populate the VarObject */
        result = VAROBJECT_CreateFromString( str,
                                             type,
                                             &varObject,
                                             VAROBJECT_OPTION_NONE );
        if( result == EOK )
        {
            /* set the sysvar using the VarObject */
            result = VAR_Set( hVarServer, hVar, &varObject );
        }
    }

    return result;
}

/*============================================================================*/
/*  VAR_Set                                                                   */
/*!
    Set a variable value in the specified var object

    The VAR_Set function sets the value of the variable
    specified by hVar to the value specified by the var object

    @param[in]
        hVarServer
            handle to the variable server

    @param[in]
        hVar
            handle to the variable to be set

    @param[in]
        pVarObject
            pointer to the variable value object to set

    @retval EOK - the variable was set ok
    @retval EINVAL - invalid arguments

==============================================================================*/
int VAR_Set( VARSERVER_HANDLE hVarServer,
             VAR_HANDLE hVar,
             VarObject *pVarObject )
{
    int result = EINVAL;
    char *p;
    size_t len;

    VarClient *pVarClient = ValidateHandle( hVarServer );

    if( ( pVarClient != NULL ) &&
        ( pVarObject != NULL ) )
    {
        pVarClient->requestType = VARREQUEST_SET;
        pVarClient->variableInfo.hVar = hVar;
        pVarClient->variableInfo.var.type = pVarObject->type;
        pVarClient->variableInfo.var.val = pVarObject->val;
        pVarClient->variableInfo.var.len = pVarObject->len;

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
            result = pVarClient->responseVal;
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
/*  VAR_GetFirst                                                              */
/*
    Start a variable query

    The VAR_GetFirst function initiates a variable query with the variable
    server.

    Variable queries can be made using a combination of the following:

    - variable name
    - variable instance ID
    - variable flags
    - variable tags

    @param[in]
        hVarServer
            handle to the variable server

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
int VAR_GetFirst( VARSERVER_HANDLE hVarServer,
                  VarQuery *query,
                  VarObject *obj )
{
    int result = EINVAL;
    char *p;
    size_t len;

    VarClient *pVarClient = ValidateHandle( hVarServer );

    if( ( pVarClient != NULL ) &&
        ( query != NULL ) &&
        ( obj != NULL ) )
    {
        pVarClient->requestType = VARREQUEST_GET_FIRST;
        pVarClient->requestVal = query->type;
        pVarClient->variableInfo.instanceID = query->instanceID;
        pVarClient->variableInfo.flags = query->flags;
        memcpy( &pVarClient->variableInfo.tagspec,
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
            }
        }

        /* send the request to the server */
        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            query->context = pVarClient->responseVal;
            if( query->context > 0 )
            {
                /* get the name of the variable we found */
                memcpy( query->name,
                        pVarClient->variableInfo.name,
                        MAX_NAME_LEN+1 );

                /* get the handle of the variable we found */
                query->hVar = pVarClient->variableInfo.hVar;
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
/*  VAR_GetNext                                                               */
/*
    Continue a variable query

    The VAR_GetNext function continues a variable search and tries to get
    the next result in the set of variable which match the initial variable
    query defined when calling VAR_GetFirst

    @param[in]
        hVarServer
            handle to the variable server

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
int VAR_GetNext( VARSERVER_HANDLE hVarServer,
                 VarQuery *query,
                 VarObject *obj )
{
    int result = EINVAL;
    char *p;
    size_t len;

    VarClient *pVarClient = ValidateHandle( hVarServer );

    if( ( pVarClient != NULL ) &&
        ( query != NULL ) &&
        ( obj != NULL ) )
    {
        pVarClient->requestType = VARREQUEST_GET_NEXT;
        pVarClient->requestVal = query->context;

        /* send the request to the server */
        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( result == EOK )
        {
            query->context = pVarClient->responseVal;
            if( query->context > 0 )
            {
                /* get the name of the variable we found */
                memcpy( query->name,
                        pVarClient->variableInfo.name,
                        MAX_NAME_LEN+1 );

                /* get the handle of the variable we found */
                query->hVar = pVarClient->variableInfo.hVar;
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
/*  VAR_Notify                                                                */
/*!
    Register a notification for a specific variable

    The VAR_Notify function requests a notification for an action
    on the specified variable.

    @param[in]
        hVarServer
            handle to the variable server

    @param[in]
        hVar
            handle to the variable to be notified on

    @param[in]
        notification
            the type of notification requested


    @retval EOK - the notification request was registered successfully
    @retval EINVAL - invalid arguments

==============================================================================*/
int VAR_Notify( VARSERVER_HANDLE hVarServer,
                VAR_HANDLE hVar,
                NotificationType notificationType )
{
    int result = EINVAL;
    VarClient *pVarClient = ValidateHandle( hVarServer );

    if( pVarClient != NULL )
    {
        pVarClient->requestType = VARREQUEST_NOTIFY;
        pVarClient->variableInfo.hVar = hVar;
        pVarClient->variableInfo.notificationType = notificationType;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
    }

    return result;
}


/*============================================================================*/
/*  VAR_Print                                                                 */
/*!
    Print a variable value to the specified output stream

    The VAR_Print function prints out the value of the variable
    specified by hVar to the output stream specified by fp.

    @param[in]
        hVarServer
            handle to the variable server

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
int VAR_Print( VARSERVER_HANDLE hVarServer,
               VAR_HANDLE hVar,
               int fd )
{
    int result = EINVAL;
    pid_t responderPID;
    int sock;

    VarClient *pVarClient = ValidateHandle( hVarServer );

    if( pVarClient != NULL )
    {
        pVarClient->requestType = VARREQUEST_PRINT;
        pVarClient->variableInfo.hVar = hVar;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
        if( pVarClient->responseVal == ESTRPIPE)
        {
            /* get the PID of the client doing the printing */
            responderPID = (pid_t)(pVarClient->peer_pid);

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
                                     &pVarClient->variableInfo,
                                     &pVarClient->workbuf );
        }
    }

    return result;
}

/*============================================================================*/
/*  VAR_OpenPrintSession                                                      */
/*!
    Open a new print session

    The VAR_OpenPrintSession creates a new print session,
    creating a link to the requesting client's output stream
    and returning a handle to the variable that should be output.

    @param[in]
        hVarServer
            handle to the variable server

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
int VAR_OpenPrintSession( VARSERVER_HANDLE hVarServer,
                         uint32_t id,
                         VAR_HANDLE *hVar,
                         int *fd )
{
    int result = EINVAL;
    VarClient *pVarClient = ValidateHandle( hVarServer );
    int sock;
    pid_t pid;

    if( ( pVarClient != NULL ) &&
        ( hVar != NULL ) &&
        ( fd != NULL ) )
    {
        /* set up a socket to get the file descriptor to print to */
        pid = pVarClient->client_pid;
        result = VARPRINT_SetupListener( pid, &sock );
        if( result == EOK )
        {
            pVarClient->requestType = VARREQUEST_OPEN_PRINT_SESSION;
            pVarClient->requestVal = id;

            result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
            if( result == EOK )
            {
                /* get a handle to the variable we are printing */
                *hVar = pVarClient->variableInfo.hVar;

                /* get the file descriptor we are printing to */
                result = VARPRINT_GetFileDescriptor( pVarClient->peer_pid,
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
/*  VAR_ClosePrintSession                                                     */
/*!
    Conclude a print session

    The VAR_ClosePrintSession terminates an active print session
    and unblocks the requesting client.

    @param[in]
        hVarServer
            handle to the variable server

    @param[in]
        id
            transaction identifier for the print session

    @param[in]
        fd
            file descriptor for the print session output stream

    @retval EOK - the print session was successfully completed
    @retval EINVAL - invalid arguments

==============================================================================*/
int VAR_ClosePrintSession( VARSERVER_HANDLE hVarServer,
                           uint32_t id,
                           int fd )
{
    int result = EINVAL;
    VarClient *pVarClient = ValidateHandle( hVarServer );

    if( pVarClient != NULL )
    {
        pVarClient->requestType = VARREQUEST_CLOSE_PRINT_SESSION;
        pVarClient->requestVal = id;

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
/*  ValidateHandle                                                            */
/*!
    Validate a variable server handle

    The ValidateHandle function checks that the specified handle is a
    handle to the variable server and converts it into VarClient
    pointer.

    @param[in]
        hVarServer
            handle to the Variable Server

    @retval pointer to a VarClient object
    @retval NULL - an invalid variable server handle was specified

==============================================================================*/
static VarClient *ValidateHandle( VARSERVER_HANDLE hVarServer )
{
    VarClient *pVarClient;

    pVarClient = (VarClient *)hVarServer;
    if( pVarClient != NULL )
    {
        if( ( pVarClient->id != VARSERVER_ID ) &&
            ( pVarClient->version != VARSERVER_VERSION ) )
        {
            if( pVarClient->debug >= LOG_ERR )
            {
                printf("CLIENT: Invalid VARSERVER handle\n");
            }
            pVarClient = NULL;
        }
    }

    return pVarClient;
}

/*============================================================================*/
/*  InitServerInfo                                                            */
/*!
    Initialize the VarServer information

    The InitServerInfo function opens a shared memory block owned by the
    variable server in which public variable server status is stored.
    The shared memory block is opened in a read-only mode to allow clients
    to get information about the server including the server's process
    identifier which is used to send signals from the client to the server.

    @retval pointer to the Variable Server's ServerInfo object
    @retval NULL - the Variable Server's ServerInfo object could not be read

==============================================================================*/
static int InitServerInfo( VarClient *pVarClient )
{
    int fd;
    int result = EINVAL;
    ServerInfo *pServerInfo = NULL;

    if( pVarClient != NULL )
    {
        /* get shared memory file descriptor (NOT a file) */
        fd = shm_open( SERVER_SHAREDMEM, O_RDONLY, S_IRUSR | S_IWUSR);
        if (fd != -1)
        {
            /* map shared memory to process address space */
            pServerInfo = (ServerInfo *)mmap( NULL,
                                            sizeof(ServerInfo),
                                            PROT_READ,
                                            MAP_SHARED,
                                            fd,
                                            0);
            if (pServerInfo != MAP_FAILED)
            {
                pVarClient->pServerInfo = pServerInfo;
                if( pVarClient->debug >= LOG_INFO )
                {
                    printf("CLIENT: Server PID: %d\n", pServerInfo->pid);
                }

                result = EOK;
            }
            else if( pVarClient->debug >= LOG_ERR )
            {
                perror("mmap");
            }
        }
        else if( pVarClient->debug >= LOG_ERR )
        {
            perror("shm_open");
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
        signal
            specifies the readl-time signal to be sent
            from the client to the server

    @retval EOK - the client request was handled successfully by the server
    @retval EINVAL - an invalid client was specified
    @retval other - error code returned by sigqueue, or sem_wait

==============================================================================*/
static int ClientRequest( VarClient *pVarClient, int signal )
{
    int result = EINVAL;
    union sigval val;
    ServerInfo *pServerInfo = NULL;

    if( pVarClient != NULL )
    {
         /* get a pointer to the server info */
        pServerInfo = pVarClient->pServerInfo;
        if( pServerInfo != NULL )
        {
            /* provide the client identifier to the var server */
            val.sival_int = pVarClient->clientid;

            if( pVarClient->debug >= LOG_DEBUG )
            {
                printf("CLIENT: Sending client request signal (%d) to %d\n",
                       signal,
                       pServerInfo->pid );
            }

            result = sigqueue( pServerInfo->pid, signal, val );
            if( result == EOK )
            {
                do
                {
                    pVarClient->blocked = 1;
                    result = sem_wait( &pVarClient->sem );
                    if( result == EOK )
                    {
                        if( pVarClient->debug >= LOG_DEBUG )
                        {
                            printf("CLIENT: Received response\n");
                        }
                    }
                    else
                    {
                        printf("sem_wait failed\n");
                        result = errno;
                    }
                    pVarClient->blocked = 0;
                }
                while ( result != EOK );
            }
            else
            {
                result = errno;
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
/*  NewClient                                                                 */
/*!
    Create a new Variable Server client

    The NewClient function creates a shared memory VarClient object
    and semaphore used to communicate with the Variable Server.
    The semaphore is used to block the client while it is awaiting a
    response from the server.  The VarClient object is used to
    send data to the variable server, and receive responses from the
    variable server.

    The variable client shared memory object is accessible via
    /varclient_<client pid>

    @param[in]
        workbufsize
            specifies the size of the working buffer interface
            between the client and the server

    @retval pointer to the newly created VarClient object
    @retval NULL if the VarClient object could not be created

==============================================================================*/
static VarClient *NewClient( size_t workbufsize )
{
    int res;
	int fd;
	int len;
	pid_t pid;
    char clientname[BUFSIZ];
    VarClient *pVarClient = NULL;
    size_t sharedMemSize;

    /* calculate the size of the client-server interface working buffer */
    sharedMemSize = sizeof(VarClient) + workbufsize;

    /* build the varclient identifier */
	pid = getpid();
	sprintf(clientname, "/varclient_%d", pid);


	/* get shared memory file descriptor (NOT a file) */
	fd = shm_open(clientname, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);

	if (fd != -1)
	{
    	/* extend shared memory object as by default
           it is initialized with size 0 */
	    res = ftruncate(fd, sharedMemSize );
	    if (res != -1)
	    {
            /* map shared memory to process address space */
            pVarClient = mmap( NULL,
                               sharedMemSize ,
                               PROT_WRITE,
                               MAP_SHARED,
                               fd,
                               0);

            if( pVarClient != NULL )
            {
                /* populate the VarClient object */
                pVarClient->id = VARSERVER_ID;
                pVarClient->version = VARSERVER_VERSION;
                pVarClient->client_pid = pid;
                pVarClient->workbufsize = workbufsize + 1;

                /* clear the working buffer */
                memset( &pVarClient->workbuf, 0, pVarClient->workbufsize );

                /* initialize the VarClient semaphore */
                NewClientSemaphore( pVarClient );
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

        /* close the file descriptor since we don't need it for anything */
        close( fd );
	}
    else
    {
        perror("shm_open");
    }

    return pVarClient;
}

/*============================================================================*/
/*  NewClientSemaphore                                                        */
/*!
    Initialize a new Variable Client semaphore

    The NewClientSemaphore function initializes the Variable Client
    semaphore before it is first used.

    The semaphore is used to block the client while it is awaiting a
    response from the server.

    @retval EOK the new client semaphore was successfully initialized
    @retval EINVAL an invalid variable client was specified

==============================================================================*/
static int NewClientSemaphore( VarClient *pVarClient )
{
    char semname[BUFSIZ];
    int result = EINVAL;

    if( pVarClient != NULL )
    {
        sem_init( &pVarClient->sem, 1, 0 );
        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  DeleteClientQueue                                                         */
/*!
    Delete a client notification queue

    The DeleteClientQueue function deletes a client notification
    queue.

==============================================================================*/
static void DeleteClientQueue( VarClient *pVarClient )
{
    char clientname[BUFSIZ];

    if ( pVarClient != NULL )
    {
        /* build the varclient identifier */
        sprintf(clientname, "/varclient_%d", pVarClient->client_pid);

        mq_close( pVarClient->notificationQ );
        mq_unlink( clientname );
    }
}

/*============================================================================*/
/*  VAR_GetFromQueue                                                          */
/*!
    Get a variable notification from the notification queue

    The VAR_GetFromQueue function gets the value of the variable
    from the notification queue and puts it into the specified
    VarNotification object.

    The caller should provide an appropriately sized buffer
    in pVarNotification->obj.val.blob to receive the maximum
    expected blob or string data.

    If a buffer is not provided, one will be created, and the
    caller must be responsible for deallocating the buffer
    when they are done with it.

    @param[in]
        hVarServer
            handle to the variable server

    @param[in]
        pVarNotification
            specifies the location where the variable value should be stored

    @param[in]
        buf
            pointer to a working buffer to receive a message

    @param[in]
        len
            length of the working buffer

    @retval EOK - the variable was retrieved ok
    @retval EAGAIN - no data is available
    @retval E2BIG - the received data is too big to fit in the supplied buffer
    @retval ENOMEM - memory allocation failed
    @retval EINVAL - invalid arguments

==============================================================================*/
int VAR_GetFromQueue( VARSERVER_HANDLE hVarServer,
                      VarNotification *pVarNotification,
                      char *buf,
                      size_t len )
{
    int result = EINVAL;
    VarClient *pVarClient = ValidateHandle( hVarServer );
    ssize_t n;
    void *p;
    size_t varlen;
    VarNotification *pSrc;

    if( ( pVarClient != NULL ) &&
        ( pVarNotification != NULL ) &&
        ( buf != NULL ) &&
        ( len > sizeof(VarNotification) ) )
    {
        n = mq_receive( pVarClient->notificationQ,
                        buf,
                        len,
                        NULL );
        if ( n >= 0 )
        {
            /* get a pointer to the received VarObject */
            pSrc = (VarNotification *)buf;

            if ( n > sizeof( VarNotification ) )
            {
                /* calculate the length of the variable part of the message */
                varlen = n - sizeof(VarNotification);

                /* see if the caller has provided a blob/string buffer */
                if ( pVarNotification->obj.val.blob == NULL )
                {
                    /* no user supplied buffer, create one */
                    pVarNotification->obj.val.blob = calloc(1, varlen );
                    if( pVarNotification->obj.val.blob == NULL )
                    {
                        /* failed to allocate memory for blob/string data */
                        result = ENOMEM;
                    }
                }

                if ( pVarNotification->obj.val.blob != NULL )
                {
                    if ( ( varlen > 0 ) &&
                         ( varlen <= pVarNotification->obj.len ) )
                    {
                        /* copy the blob data */
                        p = &buf[sizeof(VarNotification)];
                        memcpy( pVarNotification->obj.val.blob, p, varlen );

                        /* copy the blob metadata */
                        pVarNotification->obj.len = varlen;
                        pVarNotification->hVar = pSrc->hVar;
                        pVarNotification->obj.type = pSrc->obj.type;

                        result = EOK;
                    }
                    else
                    {
                        /* received blob/string data will not fit into
                           the supplied buffer */
                        result = E2BIG;
                    }
                }
            }
            else
            {
                /* copy primitive type */
                memcpy( pVarNotification, buf, sizeof(VarNotification));

                result = EOK;
            }
        }
        else
        {
            result = errno;
        }
    }

    return result;
}

/*============================================================================*/
/*  DeleteClientSemaphore                                                     */
/*!
    Delete the Variable Client semaphore

    The DeleteClientSemaphore function destroyes the Variable Client
    semaphore when it is no longer needed.

    @retval EOK the client semaphore was successfully destroyed
    @retval EINVAL an invalid variable client was specified
    @retval other error response returned from sem_destroy

==============================================================================*/
static int DeleteClientSemaphore( VarClient *pVarClient )
{
    int result = EINVAL;
    char semname[BUFSIZ];

    if( pVarClient != NULL )
    {
        result = sem_destroy( &pVarClient->sem );
        if( result != EOK )
        {
            result = errno;
        }

        if( ( result != EOK ) &&
            ( pVarClient->debug >= LOG_ERR ) )
        {
            printf("%s failed: (%d) %s\n", __func__, result, strerror(result));
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
    ServerInfo *pServerInfo = NULL;

    if( pVarClient != NULL )
    {
        /* delete the client semaphore */
        DeleteClientSemaphore( pVarClient );

        /* delete the client notification queue */
        DeleteClientQueue( pVarClient );

        /* build the varclient identifier */
        sprintf(clientname, "/varclient_%d", pVarClient->client_pid);

        /* clean up the server info structure */
        pServerInfo = pVarClient->pServerInfo;
        if( pServerInfo != NULL )
        {
            res = munmap( pServerInfo, sizeof(ServerInfo) );
            if( ( res == -1 ) &&
                ( pVarClient->debug >= LOG_ERR ) )
            {
                printf("CLIENT: unable to clean up server info\n");
            }
        }

        /* mmap cleanup */
        res = munmap( pVarClient, sizeof(VarClient) );
        if ( res != -1 )
        {
            /* shm_open cleanup */
            fd = shm_unlink( clientname );
            if (fd != -1)
            {
                result = EOK;
            }
            else if( pVarClient->debug >= LOG_ERR )
            {
                perror("CLIENT: unlink");
            }
        }
        else if( pVarClient->debug >= LOG_ERR )
        {
            perror("CLIENT: munmap");
        }
    }

    return result;
}

/*! @}
 * end of varserver_api group */
