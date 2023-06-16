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
 * @defgroup varlist varlist
 * @brief RealTime In-Memory Publish/Subscribe Key/Value variable list
 * @{
 */

/*============================================================================*/
/*!
@file varlist.c

    Variable List

    The Variable List maintains a searchable list of varserver variables.

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include "varclient.h"
#include "varserver.h"
#include "varobject.h"
#include "var.h"
#include "server.h"
#include "varstorage.h"
#include "varlist.h"
#include "taglist.h"
#include "notify.h"
#include "blocklist.h"
#include "transaction.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*! Maximum number of clients */
#ifndef VARSERVER_MAX_VARIABLES
#define VARSERVER_MAX_VARIABLES                 ( 65535 )
#endif

/*==============================================================================
        Type definitions
==============================================================================*/

/*! search context */
typedef struct _searchContext
{
    /*! context identifier */
    int contextId;

    /*! client process identifier */
    pid_t clientPID;

    /*! last variable found */
    VAR_HANDLE hVar;

    /*! query parameters */
    VarQuery query;

    /*! context */
    void *context;

    /*! pointer to the next search context */
    struct _searchContext *pNext;
} SearchContext;

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! counts the number of variables in the list */
static int varcount = 0;

/*! variable storage */
static VarStorage varstore[ VARSERVER_MAX_VARIABLES ] = {0};

/*! search contexts */
static SearchContext *pSearchContexts = NULL;

/*! unique context identifier */
static int contextIdent = 0;

/*==============================================================================
        Private function declarations
==============================================================================*/

static int AssignVarInfo( VarStorage *pVarStorage, VarInfo *pVarInfo );
static int varlist_Set16( VarStorage *pVarStorage, VarInfo *pVarInfo );
static int varlist_Set16s( VarStorage *pVarStorage, VarInfo *pVarInfo );
static int varlist_Set32( VarStorage *pVarStorage, VarInfo *pVarInfo );
static int varlist_Set32s( VarStorage *pVarStorage, VarInfo *pVarInfo );
static int varlist_Set64( VarStorage *pVarStorage, VarInfo *pVarInfo );
static int varlist_Set64s( VarStorage *pVarStorage, VarInfo *pVarInfo );
static int varlist_SetFloat( VarStorage *pVarStorage, VarInfo *pVarInfo );
static int varlist_SetStr( VarStorage *pVarStorage, VarInfo *pVarInfo );
static int varlist_SetBlob( VarStorage *pVarStorage, VarInfo *pVarInfo );
static int varlist_Calc( VarClient *pVarClient, void *arg );

static SearchContext *varlist_NewSearchContext( pid_t clientPID,
                                                int searchType,
                                                VarInfo *pVarInfo,
                                                char *searchText );
static int varlist_DeleteSearchContext( SearchContext *ctx );
static SearchContext *varlist_FindSearchContext( pid_t clientPID,
                                                 int context );

static int varlist_Match( VAR_HANDLE hVar, SearchContext *ctx );

static int assign_BlobVarInfo( VarStorage *pVarStorage, VarInfo *pVarInfo );
static int assign_StringVarInfo( VarStorage *pVarStorage, VarInfo *pVarInfo );

static int varlist_CopyVarInfoBlobToClient( VarClient *pVarClient,
                                            VarInfo *pVarInfo );
static int varlist_CopyVarInfoStringToClient( VarClient *pVarClient,
                                              VarInfo *pVarInfo );

/*==============================================================================
        Public function definitions
==============================================================================*/

/*============================================================================*/
/*  VARLIST_AddNew                                                            */
/*!
    Add a new variable to the variable list

    The VARLIST_AddNew function adds a new variable to the variable list
    It returns a handle to the newly added variable

    @param[in]
        pVarInfo
            Pointer to the new variable to add

    @param[out]
        pVarHandle
            pointer to a location to store the handle to the new variable

    @retval EOK the new variable was added
    @retval ENOMEM memory allocation failure
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_AddNew( VarInfo *pVarInfo, uint32_t *pVarHandle )
{
    int result = EINVAL;
    int varhandle;
    size_t len;
    char *pStr;
    VarStorage *pVarStorage;

    if( ( pVarInfo != NULL ) &&
        ( pVarHandle != NULL ) )
    {
        if( varcount < VARSERVER_MAX_VARIABLES )
        {
            /* assume success until we know otherwise */
            result = EOK;

            /* get the variable handle for the new variable */
            varhandle = varcount + 1;


            if( result == EOK )
            {
                /* get a pointer to the variable storage for the new variable */
                pVarStorage = &varstore[varhandle];

                /* copy the variable information from the VarInfo object
                   to the VarStorage object */
                result = AssignVarInfo( pVarStorage, pVarInfo );
                if( result == EOK )
                {
                    /* increment the number of variables */
                    varcount++;
                }
            }
        }
        else
        {
            result = ENOMEM;
        }
    }

    return result;
}

/*============================================================================*/
/*  VARLIST_Find                                                              */
/*!
    Find a variable in the variable list by its name

    The VARLIST_Find function searches the variable list for the
    variable with the specified name and instance identifier.

    @param[in]
        pVarInfo
            Pointer to the variable definition containing the name and
            instance identifier of the variable to find

    @param[in]
        pVarHandle
            pointer to a VAR_HANDLE to store the handle to the variable
            when it is found or VAR_INVALID if it is not.

    @retval EOK the variable was found
    @retval ENOENT the variable was not found
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_Find( VarInfo *pVarInfo, VAR_HANDLE *pVarHandle )
{
    int result = EINVAL;
    VAR_HANDLE hVar;

    if( ( pVarInfo != NULL ) &&
        ( pVarHandle != NULL ) )
    {
        /* assume we didn't find it */
        result = ENOENT;

        /* store VAR_INVALID in the variable handle */
        *pVarHandle = VAR_INVALID;

        /* iterate through the VarStorage */
        for( hVar = 1; hVar <= varcount; hVar++ )
        {
            /* case insensitive name comparison */
            if( ( pVarInfo->instanceID == varstore[hVar].instanceID ) &&
                ( strcasecmp( pVarInfo->name, varstore[hVar].name ) == 0 ) )
            {
                *pVarHandle = hVar;
                result = EOK;
                break;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  VARLIST_PrintByHandle                                                     */
/*!
    Handle a print request from a client

    The VARLIST_PrintByHandle function handles a print request from a
    client.  It copies the variable TLV (type-length-value) data
    and the format specifier.  If the variable is a string, its
    data is passed via the specified working buffer

    @param[in]
        clientPID
            process identifier of the requesting client

    @param[in,out]
        pVarInfo
            Pointer to the variable definition containing the handle
            of the variable to print and where the variable TLV
            data will be stored.

    @param[out]
        workbuf
            pointer to the working buffer (for returning string values)

    @param[in]
        workbufsize
            specifies the size of the working buffer (and therefore
            the maximum string length that can be retrieved).

    @param[in]
        clientInto
            opaque pointer to the client information

    @param[out]
        handler
            pointer to a location to store the PID of the handler
            process (if applicable)

    @retval EOK the variable print request was handled
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_PrintByHandle( pid_t clientPID,
                           VarInfo *pVarInfo,
                           char *workbuf,
                           size_t workbufsize,
                           void *clientInfo,
                           pid_t *handler )
{
    VarStorage *pVarStorage;
    int result = EINVAL;
    VAR_HANDLE hVar;
    size_t n;
    uint32_t printHandle;

    if( ( pVarInfo != NULL ) &&
        ( workbuf != NULL ) )
    {
        hVar = pVarInfo->hVar;

        if( ( hVar < VARSERVER_MAX_VARIABLES ) &&
            ( hVar <= varcount ) )
        {
            /* get a pointer to the variable storage for this variable */
            pVarStorage = &varstore[hVar];

            /* check if this variable has a PRINT handler attached */
            if( pVarStorage->notifyMask & NOTIFY_MASK_PRINT )
            {
                /* create a PRINT transaction */
                result = TRANSACTION_New( clientPID, clientInfo, &printHandle );
                if( result == EOK )
                {
                    /* send a PRINT notification */
                    result = NOTIFY_Signal( clientPID,
                                            &pVarStorage->pNotifications,
                                            NOTIFY_PRINT,
                                            printHandle,
                                            handler );
                    if( result == EOK )
                    {
                        /* indicate that there is now a PRINT blocked
                        client on this variable */
                        pVarStorage->notifyMask |= NOTIFY_MASK_HAS_PRINT_BLOCK;

                        /* indicate that the print output will be piped
                        from another client */
                        result = ESTRPIPE;
                    }
                }
            }

            /* check if this variable has a CALC handler attached */
            if( pVarStorage->notifyMask & NOTIFY_MASK_CALC )
            {
                /* send a calc request to the "owner" of this variable */
                result = NOTIFY_Signal( clientPID,
                                        &pVarStorage->pNotifications,
                                        NOTIFY_CALC,
                                        hVar,
                                        NULL );
                if (result == EOK )
                {
                    /* indicate that there is now a CALC blocked
                       client on this variable */
                    pVarStorage->notifyMask |= NOTIFY_MASK_HAS_CALC_BLOCK;

                    /* an EINPROGRESS result will prevent the client
                       from being unblocked until this request is complete */
                    result = EINPROGRESS;
                }
                else if( result == ESRCH )
                {
                    /* the CALC handler has died so remove the CALC flag */
                    pVarStorage->notifyMask &= ~NOTIFY_MASK_CALC;
                }
            }

            if( ( result != EINPROGRESS ) &&
                ( result != ESTRPIPE ) )
            {
                /* get the variable TLV */
                pVarInfo->var.len = pVarStorage->var.len;
                pVarInfo->var.type = pVarStorage->var.type;
                pVarInfo->var.val = pVarStorage->var.val;

                /* get the format specifier */
                memcpy( pVarInfo->formatspec,
                        pVarStorage->formatspec,
                        MAX_FORMATSPEC_LEN );

                if( pVarInfo->var.type == VARTYPE_STR )
                {
                    /* strings are passed back via the working buffer */
                    n = strlen( pVarStorage->var.val.str );
                    if( n < workbufsize )
                    {
                        /* copy the string */
                        memcpy( workbuf, pVarStorage->var.val.str, n );
                        workbuf[n] = '\0';
                    }
                    else
                    {
                        result = E2BIG;
                    }
                }

                if( pVarInfo->var.type == VARTYPE_BLOB )
                {
                    /* blobs are passed back via the working buffer */
                    n = pVarStorage->var.len;
                    if( n <= workbufsize )
                    {
                        /* copy the string */
                        memcpy( workbuf, pVarStorage->var.val.blob, n );
                    }
                    else
                    {
                        result = E2BIG;
                    }
                }

                result = EOK;
            }
        }
        else
        {
            printf("%s not found\n", __func__ );
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  VARLIST_GetByHandle                                                       */
/*!
    Handle a get request from a client

    The VARLIST_GetByHandle function handles a get request from a
    client.  It copies the variable TLV (type-length-value).
    If the variable is a string, its data is passed via the specified buffer

    @param[in,out]
        pVarInfo
            Pointer to the variable definition containing the handle
            of the variable to get and where the variable TLV
            data will be stored.

    @param[out]
        buf
            pointer to the buffer (for returning string values)

    @param[in]
        bufsize
            specifies the size of the buffer (and therefore
            the maximum string length that can be retrieved).

    @retval EOK the variable get request was handled
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_GetByHandle( pid_t clientPID,
                         VarInfo *pVarInfo,
                         char *buf,
                         size_t bufsize )
{
    VarStorage *pVarStorage;
    int result = EINVAL;
    VAR_HANDLE hVar;
    size_t n;
    uint8_t notifyType;

    if( ( pVarInfo != NULL ) &&
        ( buf != NULL ) )
    {
        hVar = pVarInfo->hVar;

        if( ( hVar < VARSERVER_MAX_VARIABLES ) &&
            ( hVar <= varcount ) )
        {
            /* get a pointer to the variable storage for this variable */
            pVarStorage = &varstore[hVar];

            /* check if this variable has a CALC handler attached */
            if( pVarStorage->notifyMask & NOTIFY_MASK_CALC )
            {
                /* send a calc request to the "owner" of this variable */
                result = NOTIFY_Signal( clientPID,
                                        &pVarStorage->pNotifications,
                                        NOTIFY_CALC,
                                        hVar,
                                        NULL );
                if (result == EOK )
                {
                    /* indicate that there is now a CALC blocked
                       client on this variable */
                    pVarStorage->notifyMask |= NOTIFY_MASK_HAS_CALC_BLOCK;

                    /* an EINPROGRESS result will prevent the client
                       from being unblocked until this request is complete */
                    result = EINPROGRESS;
                }
                else if( result == ESRCH )
                {
                    /* the CALC handler has died so remove the CALC flag */
                    pVarStorage->notifyMask &= ~NOTIFY_MASK_CALC;
                }
            }

            if( result != EINPROGRESS )
            {
                /* not a CALC notification */
                /* get the variable TLV */
                pVarInfo->var.len = pVarStorage->var.len;
                pVarInfo->var.type = pVarStorage->var.type;
                pVarInfo->var.val = pVarStorage->var.val;

                result = EOK;

                if( pVarInfo->var.type == VARTYPE_STR )
                {
                    /* strings are passed back via the working buffer */
                    n = strlen( pVarStorage->var.val.str );
                    if( n < bufsize )
                    {
                        /* copy the string */
                        memcpy( buf, pVarStorage->var.val.str, n );
                        buf[n] = '\0';
                    }
                    else
                    {
                        result = E2BIG;
                    }
                }

                if( pVarInfo->var.type == VARTYPE_BLOB )
                {
                    /* blobs are passed back via the working buffer */
                    n = pVarStorage->var.len;
                    if( n <= bufsize )
                    {
                        /* copy the blob */
                        memcpy( buf, pVarStorage->var.val.blob, n );
                    }
                    else
                    {
                        result = E2BIG;
                    }
                }
            }
        }
        else
        {
            printf("%s not found\n", __func__ );
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  VARLIST_Set                                                               */
/*!
    Handle a SET request from a client

    The VARLIST_Set function handles a SET request from a client.
    It sets the specified variable to the specified value

    For variables with a validation notification, the client will
    be blocked until the validation can be completed

    @param[in,out]
        pVarInfo
            Pointer to the variable definition containing the handle
            of the variable to set and the value to set it to

    @retval EOK the variable was successfully set
    @retval ENOTSUP the variable was not the appropriate type
    @retval ENOENT the variable does not exist
    @retval E2BIG the string variable is too big
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_Set( pid_t clientPID,
                 VarInfo *pVarInfo,
                 bool *validationInProgress,
                 void *clientInfo )
{
    int result = EINVAL;
    VarStorage *pVarStorage;
    VAR_HANDLE hVar;
    size_t n;
    VarType type;
    uint32_t validateHandle;

    if( pVarInfo != NULL )
    {
        hVar = pVarInfo->hVar;

        if( ( hVar < VARSERVER_MAX_VARIABLES ) &&
            ( hVar <= varcount ) &&
            ( validationInProgress != NULL ) )
        {
            /* get a pointer to the variable storage for this variable */
            pVarStorage = &varstore[hVar];

            /* Check if we have a validation handler on this variable */
            if( ( pVarStorage->notifyMask & NOTIFY_MASK_VALIDATE ) &&
                ( *validationInProgress == false ) )
            {
                if( NOTIFY_Find( pVarStorage->pNotifications,
                                 NOTIFY_VALIDATE,
                                 clientPID ) == NULL )
                {
                    /* create a validation transaction */
                    result = TRANSACTION_New( clientPID,
                                              clientInfo,
                                              &validateHandle );
                    if( result == EOK )
                    {
                        /* send a notification to the validation client with the
                        identifier of the validation request */
                        result = NOTIFY_Signal( clientPID,
                                                &pVarStorage->pNotifications,
                                                NOTIFY_VALIDATE,
                                                validateHandle,
                                                NULL );
                        if( result == EOK )
                        {
                            *validationInProgress = true;
                            result = EINPROGRESS;
                        }
                        else if( result == ESRCH )
                        {
                            /* the validation client is gone, so clear the
                            notification mask */
                            pVarStorage->notifyMask &= ~NOTIFY_MASK_VALIDATE;
                        }
                    }
                }
            }

            if( result != EINPROGRESS )
            {
                /* indicate that there is no validation in progress */
                *validationInProgress = false;

                switch( pVarStorage->var.type )
                {
                    case VARTYPE_BLOB:
                        result = varlist_SetBlob( pVarStorage, pVarInfo );
                        break;

                    case VARTYPE_STR:
                        result = varlist_SetStr( pVarStorage, pVarInfo );
                        break;

                    case VARTYPE_FLOAT:
                        result = varlist_SetFloat( pVarStorage, pVarInfo );
                        break;

                    case VARTYPE_UINT16:
                        result = varlist_Set16( pVarStorage, pVarInfo );
                        break;

                    case VARTYPE_INT16:
                        result = varlist_Set16s( pVarStorage, pVarInfo );
                        break;

                    case VARTYPE_UINT32:
                        result = varlist_Set32( pVarStorage, pVarInfo );
                        break;

                    case VARTYPE_INT32:
                        result = varlist_Set32s( pVarStorage, pVarInfo );
                        break;

                    case VARTYPE_UINT64:
                        result = varlist_Set64( pVarStorage, pVarInfo );
                        break;

                    case VARTYPE_INT64:
                        result = varlist_Set64s( pVarStorage, pVarInfo );
                        break;

                    default:
                        result = ENOTSUP;
                }
            }

            if ( ( result == EOK ) ||
                 ( result == EALREADY ) )
            {
                /* check for any CALC blocked clients on the variable */
                if( pVarStorage->notifyMask & NOTIFY_MASK_HAS_CALC_BLOCK )
                {
                    /* unblock the CALC blocked clients */
                    UnblockClients( hVar,
                                    NOTIFY_CALC,
                                    varlist_Calc,
                                    (void *)pVarInfo );

                    /* indicate we no longer have CALC blocked clients */
                    pVarStorage->notifyMask &= ~NOTIFY_MASK_HAS_CALC_BLOCK;
                }

                /* signal variable modified */
                if( ( pVarStorage->notifyMask & NOTIFY_MASK_MODIFIED ) &&
                    ( result == EOK ) )
                {
                    NOTIFY_Signal( clientPID,
                                   &pVarStorage->pNotifications,
                                   NOTIFY_MODIFIED,
                                   hVar,
                                   NULL );
                }
            }

            if ( result == EALREADY )
            {
                result = EOK;
            }
        }
        else
        {
            /* the requested variable does not exist */
            result = ENOENT;
        }
    }

    return result;
}

/*==============================================================================
        Private function definitions
==============================================================================*/

/*============================================================================*/
/*  varlist_Calc                                                              */
/*!
    Function to be applied while unblocking CALC blocked clients

    The varlist_Calc function copies the VarObject pointed by 'arg'
    into the specified VarClient's VarObject.

    Strings are copied directly into the client's working buffer

    @param[in,out]
        pVarClient
            Pointer to the VarClient to receive the VarObject

    @param[in]
        arg
            opaque pointer to the source VarInfo to copy

    @retval EOK the variable was successfully copied
    @retval EINVAL invalid arguments
    @retval ENOENT no data available in the source VarObject
    @retval E2BIG the string in the source VarObject will not fit in the
            client's working buffer

==============================================================================*/
static int varlist_Calc( VarClient *pVarClient, void *arg )
{
    int result = EINVAL;
    VarInfo *pVarInfo = (VarInfo *)arg;
    VarType varType;
    size_t len;
    VAR_HANDLE hVar;

    if( ( pVarClient != NULL ) &&
        ( pVarInfo != NULL ) )
    {
        hVar = pVarInfo->hVar;

        /* copy the format specifier */
        memcpy( pVarClient->variableInfo.formatspec,
                varstore[hVar].formatspec,
                MAX_FORMATSPEC_LEN );

        /* copy the variable type from the varstore */
        varType = varstore[hVar].var.type;
        pVarClient->variableInfo.var.type = varType;

        /* copy the variable length from the varstore */
        len = varstore[hVar].var.len;
        pVarClient->variableInfo.var.len = len;

        if( varType == VARTYPE_STR )
        {
            result = varlist_CopyVarInfoStringToClient( pVarClient, pVarInfo );
        }
        else if ( varType == VARTYPE_BLOB )
        {
            result = varlist_CopyVarInfoBlobToClient( pVarClient, pVarInfo );
        }
        else
        {
            /* copy the source VarObject to the VarClient */
            pVarClient->variableInfo.var.val = pVarInfo->var.val;

            result = EOK;
        }
    }

    return result;
}

/*============================================================================*/
/*  varlist_CopyVarInfoBlobToClient                                           */
/*!
    Copy a VarInfo blob into the client's working buffer

    The varlist_CopyVarInfoBlobToClient function copies the VarInfo object
    pointed to by the pVarInfo argument into the working buffer of the
    client pointed to by pVarClient

    Blobs are copied directly into the client's working buffer

    @param[in,out]
        pVarClient
            Pointer to the VarClient containing the working buffer to receive
            the blob data

    @param[in]
        pVarInfo
            pointer to the VarInfo object containing the blob to copy

    @retval EOK the blob was successfully copied
    @retval EINVAL invalid arguments
    @retval ENOENT no blob data contained in the source VarInfo object
    @retval E2BIG the blob in the source VarObject will not fit in the
            client's working buffer

==============================================================================*/
static int varlist_CopyVarInfoBlobToClient( VarClient *pVarClient,
                                            VarInfo *pVarInfo )
{
    int result = EINVAL;
    size_t destlen;

    if ( ( pVarClient != NULL ) &&
         ( pVarInfo != NULL ) )
    {
        /* get the destination blob size */
        destlen = pVarClient->variableInfo.var.len;

        /* check that the blob will fit */
        if( ( pVarInfo->var.len <= destlen ) &&
            ( destlen <= pVarClient->workbufsize ) )
        {
            if( pVarInfo->var.val.blob != NULL )
            {
                /* copy the blob value into the client's working buffer */
                memcpy( &pVarClient->workbuf,
                        pVarInfo->var.val.blob,
                        pVarInfo->var.len );

                result = EOK;
            }
            else
            {
                /* no source blob to copy */
                result = ENOENT;
            }
        }
        else
        {
            /* source blob is too big to fit in the client's buffer */
            result = E2BIG;
        }
    }

    return result;
}

/*============================================================================*/
/*  varlist_CopyVarInfoStringToClient                                         */
/*!
    Copy a VarInfo string into the client's working buffer

    The varlist_CopyVarInfoStringToClient function copies the VarInfo object
    pointed to by the pVarInfo argument into the working buffer of the
    client pointed to by pVarClient

    Strings are copied directly into the client's working buffer

    @param[in,out]
        pVarClient
            Pointer to the VarClient containing the working buffer to receive
            the string data

    @param[in]
        pVarInfo
            pointer to the VarInfo object containing the string to copy

    @retval EOK the string was successfully copied
    @retval EINVAL invalid arguments
    @retval ENOENT no string data contained in the source VarInfo object
    @retval E2BIG the string in the source VarObject will not fit in the
            client's working buffer

==============================================================================*/
static int varlist_CopyVarInfoStringToClient( VarClient *pVarClient,
                                              VarInfo *pVarInfo )
{
    int result = EINVAL;
    size_t destlen;

    if ( ( pVarClient != NULL ) &&
         ( pVarInfo != NULL ) )
    {
        /* get the destination blob size */
        destlen = pVarClient->variableInfo.var.len;

        /* check that the blob will fit */
        if( ( pVarInfo->var.len <= destlen ) &&
            ( destlen <= pVarClient->workbufsize ) )
        {
            if( pVarInfo->var.val.str != NULL )
            {
                /* copy the variable value into the client's working buffer */
                strcpy( &pVarClient->workbuf, pVarInfo->var.val.str );

                result = EOK;

            }
            else
            {
                /* no source string to copy */
                result = ENOENT;
            }
        }
        else
        {
            /* source string is too big to fit in the client's buffer */
            result = E2BIG;
        }
    }

    return result;
}

/*============================================================================*/
/*  varlist_Set16                                                             */
/*!
    Store a value into a VARTYPE_UINT16 variable

    The varlist_Set16 function stores a value into a VARTYPE_UINT16 variable.
    The function will convert the source type if possible.

    @param[in]
        pVarStorage
            pointer to the storage object for the target variable

    @param[in]
        pVarInfo
            pointer to the variable info object containing the value to set

    @retval EOK the variable was successfully set
    @retval ENOTSUP the source variable is not supported for this operation
    @retval ERANGE the source variable is outside of the range that can be
                   supported by the target variable
    @retval EINVAL invalid arguments
    @retval EALREADY the value is already set to the requested value

==============================================================================*/
static int varlist_Set16( VarStorage *pVarStorage, VarInfo *pVarInfo )
{
    int result = EINVAL;
    VarType srctype;
    uint16_t ui;

    if( ( pVarStorage != NULL ) &&
        ( pVarInfo != NULL ) )
    {
        srctype = pVarInfo->var.type;
        switch( srctype )
        {
            case VARTYPE_INT16:
                if ( pVarInfo->var.val.i < 0 )
                {
                    result = ERANGE;
                }
                else
                {
                    ui = (uint16_t)(pVarInfo->var.val.i);
                    if ( pVarStorage->var.val.ui == ui )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.ui = ui;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_UINT16:
                if ( pVarStorage->var.val.ui == pVarInfo->var.val.ui )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.ui = pVarInfo->var.val.ui;
                    result = EOK;
                }
                break;

            case VARTYPE_INT32:
                if ( ( pVarInfo->var.val.l > 65535 ) ||
                     ( pVarInfo->var.val.l < 0 ) )
                {
                    result = ERANGE;
                }
                else
                {
                    /* store int32_t sources into uint16_t target */
                    ui = (uint16_t)(pVarInfo->var.val.l);

                    if ( ui == pVarStorage->var.val.ui )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.ui = ui;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_UINT32:
                if( pVarInfo->var.val.ul > 65535 )
                {
                    result = ERANGE;
                }
                else
                {
                    /* store uint32_t source into uint16_t target */
                    ui = (uint16_t)(pVarInfo->var.val.ul);

                    if ( ui == pVarStorage->var.val.ui )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.ui = ui;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_UINT64:
                if( pVarInfo->var.val.ull > 65535 )
                {
                    result = ERANGE;
                }
                else
                {
                    /* store uint64_t source into uint16_t target */
                    ui = (uint16_t)(pVarInfo->var.val.ull);

                    if ( ui == pVarStorage->var.val.ui )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.ui = ui;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_INT64:
                if ( ( pVarInfo->var.val.ll > 65535 ) ||
                     ( pVarInfo->var.val.ll < 0 ) )
                {
                    result = ERANGE;
                }
                else
                {
                    /* store int64_t source into uint16_t target */
                    ui = (uint16_t)(pVarInfo->var.val.ll);

                    if ( ui == pVarStorage->var.val.ui )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.ui = ui;
                        result = EOK;
                    }
                }
                break;

            default:
                /* conversion not supported */
                result = ENOTSUP;
                break;
        }
    }

    return result;
}

/*============================================================================*/
/*  varlist_Set16s                                                            */
/*!
    Store a value into a VARTYPE_INT16 variable

    The varlist_Set16s function stores a value into a VARTYPE_INT16 variable.
    The function will convert the source type if possible.

    @param[in]
        pVarStorage
            pointer to the storage object for the target variable

    @param[in]
        pVarInfo
            pointer to the variable info object containing the value to set

    @retval EOK the variable was successfully set
    @retval ENOTSUP the source variable is not supported for this operation
    @retval ERANGE the source variable is outside of the range that can be
                   supported by the target variable
    @retval EINVAL invalid arguments
    @retval EALREADY the value is already set to the requested value

==============================================================================*/
static int varlist_Set16s( VarStorage *pVarStorage, VarInfo *pVarInfo )
{
    int result = EINVAL;
    VarType srctype;
    uint16_t ui;
    int16_t i;

    if( ( pVarStorage != NULL ) &&
        ( pVarInfo != NULL ) )
    {
        srctype = pVarInfo->var.type;
        switch( srctype )
        {
            case VARTYPE_INT16:
                if ( pVarStorage->var.val.i == pVarInfo->var.val.i )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.i = pVarInfo->var.val.i;
                    result = EOK;
                }
                break;

            case VARTYPE_UINT16:
                if ( pVarInfo->var.val.ui > 32767 )
                {
                    result = ERANGE;
                }
                else
                {
                    /* store uint16_t source into int16_t target */
                    i = (int16_t)(pVarInfo->var.val.ui);
                    if ( i == pVarStorage->var.val.i )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.i = i;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_INT32:
                if( ( pVarInfo->var.val.l > 32767 ) ||
                    ( pVarInfo->var.val.l < -32768 ) )
                {
                    result = ERANGE;
                }
                else
                {
                    /* store int32_t source into int16_t target */
                    i = (int16_t)(pVarInfo->var.val.l);

                    if ( i == pVarStorage->var.val.i )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.i = i;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_UINT32:
                if( pVarInfo->var.val.ul > 32767 )
                {
                    result = ERANGE;
                }
                else
                {
                    /* store uint32_t source into int16_t target */
                    i = (int16_t)(pVarInfo->var.val.ul);

                    if ( i == pVarStorage->var.val.i )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.i = i;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_INT64:
                if( ( pVarInfo->var.val.ll > 32767 ) ||
                    ( pVarInfo->var.val.ll < -32768 ) )
                {
                    result = ERANGE;
                }
                else
                {
                    /* store int64_t source into int16_t target */
                    i = (int16_t)(pVarInfo->var.val.ll);

                    if ( i == pVarStorage->var.val.i )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.i = i;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_UINT64:
                if( pVarInfo->var.val.ull > 32767 )
                {
                    result = ERANGE;
                }
                else
                {
                    /* store uint64_t source into int16_t target */
                    i = (int16_t)(pVarInfo->var.val.ull);

                    if ( i == pVarStorage->var.val.i )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.i = i;
                        result = EOK;
                    }
                }
                break;

            default:
                /* conversion not supported */
                result = ENOTSUP;
                break;
        }
    }

    return result;
}

/*============================================================================*/
/*  varlist_Set32                                                             */
/*!
    Store a value into a VARTYPE_UINT32 variable

    The varlist_Set32 function stores a value into a VARTYPE_UINT32 variable.
    The function will convert the source type if possible.

    @param[in]
        pVarStorage
            pointer to the storage object for the target variable

    @param[in]
        pVarInfo
            pointer to the variable info object containing the value to set

    @retval EOK the variable was successfully set
    @retval ENOTSUP the source variable is not supported for this operation
    @retval EINVAL invalid arguments
    @retval EALREADY the value is already set to the requested value

==============================================================================*/
static int varlist_Set32( VarStorage *pVarStorage, VarInfo *pVarInfo )
{
    int result = EINVAL;
    VarType srctype;
    uint32_t ul;

    if( ( pVarStorage != NULL ) &&
        ( pVarInfo != NULL ) )
    {
        srctype = pVarInfo->var.type;
        switch( srctype )
        {
            case VARTYPE_UINT32:
                if( pVarStorage->var.val.ul == pVarInfo->var.val.ul )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.ul = pVarInfo->var.val.ul;
                    result = EOK;
                }
                break;

            case VARTYPE_INT32:
                if ( pVarInfo->var.val.l < 0 )
                {
                    result = ERANGE;
                }
                else
                {
                    ul = (uint32_t)(pVarInfo->var.val.l);
                    if( pVarStorage->var.val.ul == ul )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.ul = ul;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_INT16:
                if ( pVarInfo->var.val.i < 0 )
                {
                    result = ERANGE;
                }
                else
                {
                    ul = pVarInfo->var.val.ui;
                    if ( ul == pVarStorage->var.val.ul )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.ul = ul;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_UINT16:
                ul = pVarInfo->var.val.ui;
                if ( ul == pVarStorage->var.val.ul )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.ul = ul;
                    result = EOK;
                }

            case VARTYPE_UINT64:
                if ( pVarInfo->var.val.ull > 4294967295 )
                {
                    result = ERANGE;
                }
                else
                {
                    ul = (uint32_t)(pVarInfo->var.val.ull);
                    if ( pVarStorage->var.val.ul == ul )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.ul = ul;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_INT64:
                if ( ( pVarInfo->var.val.ll > 4294967295 ) ||
                     ( pVarInfo->var.val.ll < 0 ) )
                {
                    result = ERANGE;
                }
                else
                {
                    ul = (uint32_t)(pVarInfo->var.val.ll);
                    if ( pVarStorage->var.val.ul == ul )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.ul = ul;
                        result = EOK;
                    }
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
/*  varlist_Set32s                                                            */
/*!
    Store a value into a VARTYPE_INT32 variable

    The varlist_Set32s function stores a value into a VARTYPE_INT32 variable.
    The function will convert the source type if possible.

    @param[in]
        pVarStorage
            pointer to the storage object for the target variable

    @param[in]
        pVarInfo
            pointer to the variable info object containing the value to set

    @retval EOK the variable was successfully set
    @retval ENOTSUP the source variable is not supported for this operation
    @retval EINVAL invalid arguments
    @retval EALREADY the value is already set to the requested value

==============================================================================*/
static int varlist_Set32s( VarStorage *pVarStorage, VarInfo *pVarInfo )
{
    int result = EINVAL;
    VarType srctype;
    int32_t l;

    if( ( pVarStorage != NULL ) &&
        ( pVarInfo != NULL ) )
    {
        srctype = pVarInfo->var.type;
        switch( srctype )
        {
            case VARTYPE_UINT32:
                if ( pVarInfo->var.val.ul > 2147483647 )
                {
                    result = ERANGE;
                }
                else
                {
                    l = (int32_t)(pVarInfo->var.val.ul);
                    if( pVarStorage->var.val.l == l )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.l = l;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_INT32:
                if ( pVarStorage->var.val.l == pVarInfo->var.val.l )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.l = pVarInfo->var.val.l;
                    result = EOK;
                }
                break;

            case VARTYPE_INT16:
                l = (int32_t)(pVarInfo->var.val.i);
                if ( pVarStorage->var.val.l == l )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.l = l;
                    result = EOK;
                }
                break;

            case VARTYPE_UINT16:
                l = (int32_t)(pVarInfo->var.val.ui);
                if ( pVarStorage->var.val.l == l )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.l = l;
                    result = EOK;
                }
                break;

            case VARTYPE_UINT64:
                if ( pVarInfo->var.val.ull > 2147483647 )
                {
                    result = ERANGE;
                }
                else
                {
                    l = (int32_t)(pVarInfo->var.val.ull);
                    if ( pVarStorage->var.val.l == l )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.l = l;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_INT64:
                if ( ( pVarInfo->var.val.ll > 2147483647 ) ||
                     ( pVarInfo->var.val.ll < -2147483648 ) )
                {
                    result = ERANGE;
                }
                else
                {
                    l = (int32_t)(pVarInfo->var.val.ll);
                    if ( pVarStorage->var.val.l == l )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.l = l;
                        result = EOK;
                    }
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
/*  varlist_Set64                                                             */
/*!
    Store a value into a VARTYPE_UINT64 variable

    The varlist_Set64 function stores a value into a VARTYPE_UINT64 variable.
    The function will convert the source type if possible.

    @param[in]
        pVarStorage
            pointer to the storage object for the target variable

    @param[in]
        pVarInfo
            pointer to the variable info object containing the value to set

    @retval EOK the variable was successfully set
    @retval ENOTSUP the source variable is not supported for this operation
    @retval EINVAL invalid arguments
    @retval EALREADY the value is already set to the requested value

==============================================================================*/
static int varlist_Set64( VarStorage *pVarStorage, VarInfo *pVarInfo )
{
    int result = EINVAL;
    VarType srctype;
    uint64_t ull;

    if( ( pVarStorage != NULL ) &&
        ( pVarInfo != NULL ) )
    {
        srctype = pVarInfo->var.type;
        switch( srctype )
        {
            case VARTYPE_UINT32:
                ull = (uint64_t)(pVarInfo->var.val.ul);
                if( pVarStorage->var.val.ull == ull )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.ull = ull;
                    result = EOK;
                }
                break;

            case VARTYPE_INT32:
                if ( pVarInfo->var.val.l < 0 )
                {
                    result = ERANGE;
                }
                else
                {
                    ull = (uint64_t)(pVarInfo->var.val.l);
                    if( pVarStorage->var.val.ull == ull )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.ull = ull;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_INT16:
                if ( pVarInfo->var.val.i < 0 )
                {
                    result = ERANGE;
                }
                else
                {
                    ull = (uint64_t)(pVarInfo->var.val.i);
                    if ( ull == pVarStorage->var.val.ull )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.ull = ull;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_UINT16:
                ull = (uint64_t)(pVarInfo->var.val.ui);
                if ( ull == pVarStorage->var.val.ull )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.ull = ull;
                    result = EOK;
                }

            case VARTYPE_UINT64:
                if ( pVarInfo->var.val.ull == pVarStorage->var.val.ull )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.ull = pVarInfo->var.val.ull;
                    result = EOK;
                }
                break;

            case VARTYPE_INT64:
                if ( pVarInfo->var.val.ll < 0 )
                {
                    result = ERANGE;
                }
                else
                {
                    ull = (uint64_t)(pVarInfo->var.val.ll);
                    if ( pVarStorage->var.val.ull == ull )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.ull = ull;
                        result = EOK;
                    }
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
/*  varlist_Set64s                                                            */
/*!
    Store a value into a VARTYPE_INT64 variable

    The varlist_Set64s function stores a value into a VARTYPE_UINT64 variable.
    The function will convert the source type if possible.

    @param[in]
        pVarStorage
            pointer to the storage object for the target variable

    @param[in]
        pVarInfo
            pointer to the variable info object containing the value to set

    @retval EOK the variable was successfully set
    @retval ENOTSUP the source variable is not supported for this operation
    @retval EINVAL invalid arguments
    @retval EALREADY the value is already set to the requested value

==============================================================================*/
static int varlist_Set64s( VarStorage *pVarStorage, VarInfo *pVarInfo )
{
    int result = EINVAL;
    VarType srctype;
    int64_t ll;

    if( ( pVarStorage != NULL ) &&
        ( pVarInfo != NULL ) )
    {
        srctype = pVarInfo->var.type;
        switch( srctype )
        {
            case VARTYPE_UINT32:
                ll = (int64_t)(pVarInfo->var.val.ul);
                if( pVarStorage->var.val.ll == ll )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.ll = ll;
                    result = EOK;
                }
                break;

            case VARTYPE_INT32:
                ll = (int64_t)(pVarInfo->var.val.l);
                if( pVarStorage->var.val.ll == ll )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.ll = ll;
                    result = EOK;
                }
                break;

            case VARTYPE_INT16:
                ll = (int64_t)(pVarInfo->var.val.i);
                if ( ll == pVarStorage->var.val.ll )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.ll = ll;
                    result = EOK;
                }
                break;

            case VARTYPE_UINT16:
                ll = (int64_t)(pVarInfo->var.val.ui);
                if ( ll == pVarStorage->var.val.ll )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.ll = ll;
                    result = EOK;
                }
                break;

            case VARTYPE_UINT64:
                if ( pVarInfo->var.val.ull > 9223372036854775807 )
                {
                    result = ERANGE;
                }
                else
                {
                    ll = (int64_t)(pVarInfo->var.val.ull);
                    if ( ll == pVarStorage->var.val.ull )
                    {
                        result = EALREADY;
                    }
                    else
                    {
                        pVarStorage->var.val.ll = ll;
                        result = EOK;
                    }
                }
                break;

            case VARTYPE_INT64:
                if ( pVarInfo->var.val.ll == pVarStorage->var.val.ll )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.ll = pVarInfo->var.val.ll;
                    result = EOK;
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
/*  varlist_SetFloat                                                          */
/*!
    Store a value into a VARTYPE_FLOAT variable

    The varlist_SetFloat function stores a value into a VARTYPE_FLOAT variable
    The function will convert the source type if possible.

    @param[in]
        pVarStorage
            pointer to the storage object for the target variable

    @param[in]
        pVarInfo
            pointer to the variable info object containing the value to set

    @retval EOK the variable was successfully set
    @retval ENOTSUP the source variable is not supported for this operation
    @retval EINVAL invalid arguments
    @retval EALREADY the value is already set to the requested value

==============================================================================*/
static int varlist_SetFloat( VarStorage *pVarStorage, VarInfo *pVarInfo )
{
    int result = EINVAL;
    VarType srctype;
    float f;

    if( ( pVarStorage != NULL ) &&
        ( pVarInfo != NULL ) )
    {
        srctype = pVarInfo->var.type;
        switch( srctype )
        {
            case VARTYPE_INT32:
                f = pVarInfo->var.val.l;
                if ( pVarStorage->var.val.f == f )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.f = f;
                    result = EOK;
                }
                break;

            case VARTYPE_UINT32:
                f = pVarInfo->var.val.ul;
                if ( pVarStorage->var.val.f == f )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.f = f;
                    result = EOK;
                }
                break;

            case VARTYPE_INT16:
                f = pVarInfo->var.val.i;
                if ( pVarStorage->var.val.f == f )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.f = f;
                    result = EOK;
                }
                break;

            case VARTYPE_UINT16:
                f = pVarInfo->var.val.ui;
                if ( pVarStorage->var.val.f == f )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.f = f;
                    result = EOK;
                }
                break;

            case VARTYPE_FLOAT:
                if ( pVarStorage->var.val.f == pVarInfo->var.val.f )
                {
                    result = EALREADY;
                }
                else
                {
                    pVarStorage->var.val.f = pVarInfo->var.val.f;
                    result = EOK;
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
/*  varlist_SetStr                                                            */
/*!
    Store a value into a VARTYPE_STR variable

    The varlist_SetStr function stores a value into a VARTYPE_STR variable
    The function will convert the source type if possible.

    @param[in]
        pVarStorage
            pointer to the storage object for the target variable

    @param[in]
        pVarInfo
            pointer to the variable info object containing the value to set

    @retval EOK the variable was successfully set
    @retval ENOTSUP the source variable is not supported for this operation
    @retval E2BIG not enough space to store the source variable
    @retval EINVAL invalid arguments
    @retval EALREADY the value is already set to the requested value

==============================================================================*/
static int varlist_SetStr( VarStorage *pVarStorage, VarInfo *pVarInfo )
{
    int result = EINVAL;
    size_t n;

    if( ( pVarStorage != NULL ) &&
        ( pVarInfo != NULL ) )
    {
        if( pVarInfo->var.type == VARTYPE_STR )
        {
            /* we have a string, check its length */
            n = pVarInfo->var.len;
            if( n < pVarStorage->var.len )
            {
                if ( memcmp( pVarStorage->var.val.str,
                            pVarInfo->var.val.str,
                            n ) == 0 )
                {
                    result = EALREADY;
                }
                else
                {
                    /* copy the string and nul terminate */
                    memcpy( pVarStorage->var.val.str,
                            pVarInfo->var.val.str,
                            n );
                    pVarStorage->var.val.str[n] = '\0';

                    /* string copied successfully */
                    result = EOK;
                }
            }
            else
            {
                /* the string doesn't fit */
                result = E2BIG;
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
/*  varlist_SetBlob                                                           */
/*!
    Store a value into a VARTYPE_BLOB variable

    The varlist_SetBlob function stores a value into a VARTYPE_BLOB variable

    @param[in]
        pVarStorage
            pointer to the storage object for the target variable

    @param[in]
        pVarInfo
            pointer to the variable info object containing the value to set

    @retval EOK the variable was successfully set
    @retval ENOTSUP the source variable is not supported for this operation
    @retval E2BIG not enough space to store the source variable
    @retval EINVAL invalid arguments
    @retval EALREADY the value is already set to the requested value

==============================================================================*/
static int varlist_SetBlob( VarStorage *pVarStorage, VarInfo *pVarInfo )
{
    int result = EINVAL;
    size_t n;

    if( ( pVarStorage != NULL ) &&
        ( pVarInfo != NULL ) )
    {
        if( pVarInfo->var.type == VARTYPE_BLOB )
        {
            /* we have a blob, check its length */
            n = pVarInfo->var.len;
            if( n < pVarStorage->var.len )
            {
                if ( memcmp( pVarStorage->var.val.blob,
                            pVarInfo->var.val.blob,
                            n ) == 0 )
                {
                    result = EALREADY;
                }
                else
                {
                    /* copy the blob */
                    memcpy( pVarStorage->var.val.blob,
                            pVarInfo->var.val.blob,
                            n );

                    /* blob copied successfully */
                    result = EOK;
                }
            }
            else
            {
                /* the blob doesn't fit */
                result = E2BIG;
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
/*  VARLIST_GetType                                                           */
/*!
    Handle a TYPE request from a client

    The VARLIST_Type function handles a TYPE request from a client.
    It retrieves the VarObject type for the specified variable

    @param[in,out]
        pVarInfo
            Pointer to the variable definition containing the handle
            of the variable to query

    @retval EOK the variable was successfully set
    @retval ENOENT the variable does not exist
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_GetType( VarInfo *pVarInfo )
{
    int result = EINVAL;
    VarStorage *pVarStorage;
    VAR_HANDLE hVar;
    char buf[BUFSIZ];

    if( pVarInfo != NULL )
    {
        hVar = pVarInfo->hVar;

        if( ( hVar < VARSERVER_MAX_VARIABLES ) &&
            ( hVar <= varcount ) )
        {
            /* get a pointer to the variable storage for this variable */
            pVarStorage = &varstore[hVar];

            /* retrieve the type */
            pVarInfo->var.type = pVarStorage->var.type;

            result = EOK;
        }
        else
        {
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  VARLIST_GetName                                                           */
/*!
    Handle a NAME request from a client

    The VARLIST_GetName function handles a NAME request from a client.
    It retrieves the VarObject name for the specified variable

    @param[in,out]
        pVarInfo
            Pointer to the variable definition containing the handle
            of the variable to query

    @retval EOK the variable was successfully set
    @retval ENOENT the variable does not exist
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_GetName( VarInfo *pVarInfo )
{
    int result = EINVAL;
    VarStorage *pVarStorage;
    VAR_HANDLE hVar;

    if( pVarInfo != NULL )
    {
        hVar = pVarInfo->hVar;

        if( ( hVar < VARSERVER_MAX_VARIABLES ) &&
            ( hVar <= varcount ) )
        {
            /* get a pointer to the variable storage for this variable */
            pVarStorage = &varstore[hVar];

            /* retrieve the name */
            memcpy( pVarInfo->name, pVarStorage->name, MAX_NAME_LEN+1 );
            result = EOK;
        }
        else
        {
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  VARLIST_GetLength                                                         */
/*!
    Handle a LENGTH request from a client

    The VARLIST_GetLength function handles a LENGTH request from a client.
    It retrieves the VarObject length for the specified variable

    @param[in,out]
        pVarInfo
            Pointer to the variable definition containing the handle
            of the variable to query

    @retval EOK the variable was successfully set
    @retval ENOENT the variable does not exist
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_GetLength( VarInfo *pVarInfo )
{
    int result = EINVAL;
    VarStorage *pVarStorage;
    VAR_HANDLE hVar;
    char buf[BUFSIZ];

    if( pVarInfo != NULL )
    {
        hVar = pVarInfo->hVar;

        if( ( hVar < VARSERVER_MAX_VARIABLES ) &&
            ( hVar <= varcount ) )
        {
            /* get a pointer to the variable storage for this variable */
            pVarStorage = &varstore[hVar];

            /* retrieve the type */
            pVarInfo->var.len = pVarStorage->var.len;
            result = EOK;
        }
        else
        {
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  VARLIST_RequestNotify                                                     */
/*!
    Handle a notification registration request from a client

    The VARLIST_RequestNotify function handles a notification request
    from a client.  It registers the specific notification request
    against the specified variable.

    @param[in,out]
        pVarInfo
            Pointer to the variable definition containing the handle
            of the variable to register the notification against

    @param[in]
        pid
            process ID of the requester

    @retval EOK the notification request was successfully registered
    @retval ENOENT the variable does not exist
    @retval ENOTSUP the notification type is not supported
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_RequestNotify( VarInfo *pVarInfo, pid_t pid )
{
    int result = EINVAL;
    VarStorage *pVarStorage;
    VAR_HANDLE hVar;
    NotificationType notifyType;

    if( pVarInfo != NULL )
    {
        hVar = pVarInfo->hVar;

        /* get the notification type */
        notifyType = pVarInfo->notificationType;

        if( ( hVar < VARSERVER_MAX_VARIABLES ) &&
            ( hVar <= varcount ) )
        {
            /* get a pointer to the variable storage for this variable */
            pVarStorage = &varstore[hVar];

            if( notifyType == NOTIFY_MODIFIED )
            {
                result = NOTIFY_Add( &pVarStorage->pNotifications,
                                     notifyType,
                                     pid );
                if( result == EOK )
                {
                    pVarStorage->notifyMask |= NOTIFY_MASK_MODIFIED;
                }
            }
            else if( notifyType == NOTIFY_CALC )
            {
                result = NOTIFY_Add( &pVarStorage->pNotifications,
                                     notifyType,
                                     pid );
                if( result == EOK )
                {
                    pVarStorage->notifyMask |= NOTIFY_MASK_CALC;
                }
            }
            else if( notifyType == NOTIFY_VALIDATE )
            {
                result = NOTIFY_Add( &pVarStorage->pNotifications,
                                     notifyType,
                                     pid );
                if( result == EOK )
                {
                    pVarStorage->notifyMask |= NOTIFY_MASK_VALIDATE;
                }
            }
            else if( notifyType == NOTIFY_PRINT )
            {
                result = NOTIFY_Add( &pVarStorage->pNotifications,
                                     notifyType,
                                     pid );
                if( result == EOK )
                {
                    pVarStorage->notifyMask |= NOTIFY_MASK_PRINT;
                }
            }
            else
            {
                /* other notification types are not supported at this time */
                result = ENOTSUP;
            }
        }
    }

    return result;
}


/*============================================================================*/
/*  AssignVarInfo                                                             */
/*!
    Copy the variable info from the VarInfo object to the VarStorage object

    The AssignVarInfo function copies the variable information from the
    specified VarInfo object into the specified VarStorage object.

    VarStorage <- VarInfo

    It allocates memory for string and blob type variables

    @param[in]
        pVarStorage
            Pointer to the destination VarStorage object

    @param[in]
        pVarInfo
            Pointer to the source VarInfo object

    @retval EOK the variable information was successfully assigned
    @retval ENOMEM memory allocation failure
    @retval ENOTSUP zero length string specified
    @retval EINVAL invalid arguments

==============================================================================*/
static int AssignVarInfo( VarStorage *pVarStorage, VarInfo *pVarInfo )
{
    int result = EINVAL;

    if( ( pVarStorage != NULL ) &&
        ( pVarInfo != NULL ) )
    {
        /* assume success until we know otherwise */
        /* see if we need to allocate storage for a string variable */
        result = EOK;

        if( pVarInfo->var.type == VARTYPE_STR )
        {
            result = assign_StringVarInfo( pVarStorage, pVarInfo );
        }
        else if ( pVarInfo->var.type == VARTYPE_BLOB )
        {
            result = assign_BlobVarInfo( pVarStorage, pVarInfo );
        }

        if( result == EOK )
        {
            /* set the variable's instance identifier */
            pVarStorage->instanceID = pVarInfo->instanceID;

            /* copy the variable name */
            strncpy(pVarStorage->name, pVarInfo->name, MAX_NAME_LEN );
            pVarStorage->name[MAX_NAME_LEN-1] = 0;

            /* set the variable's GUID */
            pVarStorage->guid = pVarInfo->guid;

            /* set the variable type */
            pVarStorage->var.type = pVarInfo->var.type;

            /* set the variable length */
            pVarStorage->var.len = pVarInfo->var.len;

            /* set the variable flags */
            pVarStorage->flags = pVarInfo->flags;

            /* copy the variable format specifier */
            strncpy( pVarStorage->formatspec,
                        pVarInfo->formatspec,
                        MAX_FORMATSPEC_LEN );
            pVarStorage->formatspec[MAX_FORMATSPEC_LEN] = 0;

            /* set the variable permissions */
            pVarStorage->permissions = pVarInfo->permissions;

            /* copy the variable value */
            if( ( pVarInfo->var.type != VARTYPE_STR ) &&
                ( pVarInfo->var.type != VARTYPE_BLOB ) )
            {
                pVarStorage->var.val  = pVarInfo->var.val;
            }

            /* set the variable tags */
            result = TAGLIST_Parse( pVarInfo->tagspec,
                                    pVarStorage->tags,
                                    MAX_TAGS_LEN );
        }
    }

    return result;

}

/*============================================================================*/
/*  assign_BlobVarInfo                                                        */
/*!
    Copy the blob variable info from the VarInfo object to the VarStorage object

    The assign_BlobVarInfo function copies the blob variable information from
    the specified VarInfo object into the specified VarStorage object.

    VarStorage <- VarInfo

    It allocates memory for the blob data

    @param[in]
        pVarStorage
            Pointer to the destination VarStorage object

    @param[in]
        pVarInfo
            Pointer to the source VarInfo object

    @retval EOK the variable information was successfully assigned
    @retval ENOMEM memory allocation failure
    @retval ENOTSUP zero length blob specified or invalid type specified
    @retval EINVAL invalid arguments

==============================================================================*/
static int assign_BlobVarInfo( VarStorage *pVarStorage, VarInfo *pVarInfo )
{
    int result = EINVAL;

    if ( ( pVarStorage != NULL ) &&
         ( pVarInfo != NULL ) )
    {
        if ( pVarInfo->var.type == VARTYPE_BLOB )
        {
            if( pVarInfo->var.len != 0 )
            {
                pVarStorage->var.val.blob = calloc( 1, pVarInfo->var.len );
                if( pVarStorage->var.val.blob != NULL )
                {
                    /* copy the blob */
                    memcpy( pVarStorage->var.val.blob,
                             pVarInfo->var.val.blob,
                             pVarInfo->var.len );

                    result = EOK;
                }
                else
                {
                    result = ENOMEM;
                }
            }
            else
            {
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
/*  assign_StringVarInfo                                                      */
/*!
    Copy the string var info from the VarInfo object to the VarStorage object

    The assign_StringVarInfo function copies the string variable information
    from the specified VarInfo object into the specified VarStorage object.

    VarStorage <- VarInfo

    It allocates memory for the string

    @param[in]
        pVarStorage
            Pointer to the destination VarStorage object

    @param[in]
        pVarInfo
            Pointer to the source VarInfo object

    @retval EOK the variable information was successfully assigned
    @retval ENOMEM memory allocation failure
    @retval ENOTSUP zero length string specified or invalid type specified
    @retval EINVAL invalid arguments

==============================================================================*/
static int assign_StringVarInfo( VarStorage *pVarStorage, VarInfo *pVarInfo )
{
    int result = EINVAL;

    if ( ( pVarStorage != NULL ) &&
         ( pVarInfo != NULL ) )
    {
        if ( pVarInfo->var.type == VARTYPE_STR )
        {
            if( pVarInfo->var.len != 0 )
            {
                /* allocate memroy for the string */
                pVarStorage->var.val.str = calloc( 1, pVarInfo->var.len );
                if( pVarStorage->var.val.str != NULL )
                {
                    /* copy the string */
                    strncpy( pVarStorage->var.val.str,
                             pVarInfo->var.val.str,
                             pVarInfo->var.len );

                    pVarStorage->var.val.str[ pVarInfo->var.len-1 ] = 0;

                    result = EOK;
                }
                else
                {
                    result = ENOMEM;
                }
            }
            else
            {
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
/*  VARLIST_GetFirst                                                          */
/*!
    Handle a get_first request from a client

    The VARLIST_GetFirst function handles a get_first request from a
    client.  It searches through the variable list to find the first
    variable which matches the search critera.

    @param[in]
        clientPID
            the process identifier of the requesting client

    @param[in]
        searchType
            search type bitfield containing one or more of the
            following search type bits:
                QUERY_REGEX
                QUERY_MATCH
                QUERY_IMATCH
                QUERY_FLAGS
                QUERY_TAGS

    @param[in,out]
        pVarInfo
            Pointer to the variable definition containing the search criteria

    @param[out]
        buf
            pointer to the buffer (for returning string values)

    @param[in]
        bufsize
            specifies the size of the buffer (and therefore
            the maximum string length that can be retrieved).

    @param[in,out]
        context
            pointer to a location to store the search context

    @retval EOK the variable get_first request was handled
    @retval EINVAL invalid arguments
    @retval ENOENT no variable matching the search criteria was found
    @retval ENOMEM no search contexts available

==============================================================================*/
int VARLIST_GetFirst( pid_t clientPID,
                      int searchType,
                      VarInfo *pVarInfo,
                      char *buf,
                      size_t bufsize,
                      int *context )
{
    int result = EINVAL;
    VAR_HANDLE hVar;
    size_t n;
    uint8_t notifyType;
    SearchContext *ctx;
    VarStorage *pVarStorage;

    if( ( pVarInfo != NULL ) &&
        ( buf != NULL ) &&
        ( context != NULL ) )
    {
        hVar = 1;
        result = ENOENT;

        /* create a new search context */
        ctx = varlist_NewSearchContext( clientPID,
                                        searchType,
                                        pVarInfo,
                                        buf );
        if( ctx != NULL )
        {
            /* search through the variable list */
            while( ( hVar < VARSERVER_MAX_VARIABLES ) &&
                   ( hVar <= varcount ) )
            {
                if( varlist_Match( hVar, ctx ) == EOK )
                {
                    /* get a pointer to the storage for this variable */
                    pVarStorage = &varstore[hVar];

                    /* copy the name */
                    memcpy( pVarInfo->name, pVarStorage->name, MAX_NAME_LEN+1 );

                    /* copy the format specifier */
                    memcpy( pVarInfo->formatspec,
                            pVarStorage->formatspec,
                            MAX_FORMATSPEC_LEN );

                    /* store the variable context id */
                    *context = ctx->contextId;

                    /* store the last variable found */
                    ctx->hVar = hVar;

                    /* get the variable value */
                    pVarInfo->hVar = hVar;
                    result = VARLIST_GetByHandle( clientPID,
                                                  pVarInfo,
                                                  buf,
                                                  bufsize );
                    break;
                }

                hVar++;
            }

            if( result == ENOENT )
            {
                /* nothing found - remove the search context */
                varlist_DeleteSearchContext( ctx );
                *context = 0;
            }
        }
        else
        {
            result = ENOMEM;
        }
    }

    return result;
}

/*============================================================================*/
/*  VARLIST_GetNext                                                           */
/*!
    Handle a get_next request from a client

    The VARLIST_GetNext function handles a get_next request from a
    client.  It searches through the variable list to find the next
    variable which matches the search critera in the specified search
    context

    @param[in]
        clientPID
            the process identifier of the requesting client

    @param[in]
        context
            pointer to the search context identifier

    @param[in,out]
        pVarInfo
            Pointer to the data structure which will receive the
            variable value

    @param[out]
        buf
            pointer to the buffer (for returning string values)

    @param[in]
        bufsize
            specifies the size of the buffer (and therefore
            the maximum string length that can be retrieved).

    @param[in,out]
       response
            pointer to a location to store the response value

    @retval EOK the variable get_first request was handled
    @retval EINVAL invalid arguments
    @retval ENOENT no variable matching the search criteria was found
    @retval ENOMEM no search contexts available

==============================================================================*/
int VARLIST_GetNext( pid_t clientPID,
                     int context,
                     VarInfo *pVarInfo,
                     char *buf,
                     size_t bufsize,
                     int *response )
{
    int result = EINVAL;
    VAR_HANDLE hVar;
    size_t n;
    uint8_t notifyType;
    SearchContext *ctx;
    VarStorage *pVarStorage;

    /* get the search context */
    ctx = varlist_FindSearchContext( clientPID, context );
    if ( ctx != NULL )
    {
        result = ENOENT;

        ctx->hVar++;
        hVar = ctx->hVar;

        /* search through the variable list looking for a match */
        while( ( hVar < VARSERVER_MAX_VARIABLES ) &&
               ( hVar <= varcount ) )
        {
            /* check if the current variable matches the search criteria */
            if( varlist_Match( hVar, ctx ) == EOK )
            {
                /* get a pointer to the variable storage for this variable */
                pVarStorage = &varstore[hVar];

                /* copy the name */
                memcpy( pVarInfo->name, pVarStorage->name, MAX_NAME_LEN+1 );

                /* copy the format specifier */
                memcpy( pVarInfo->formatspec,
                        pVarStorage->formatspec,
                        MAX_FORMATSPEC_LEN );

                /* store the last variable found */
                ctx->hVar = hVar;

                /* get the variable value */
                pVarInfo->hVar = hVar;
                result = VARLIST_GetByHandle( clientPID,
                                              pVarInfo,
                                              buf,
                                              bufsize );
                break;
            }

            hVar++;
        }

        if( result == ENOENT )
        {
            /* no more matching variables found */
            /* delete the search context */
            varlist_DeleteSearchContext( ctx );

            /* indicate end of search */
            context = 0;
        }

        /* store the response value */
        *response = context;
    }
    else
    {
        /* search context does not exist */
        result = ENOTSUP;
    }

    return result;
}

/*============================================================================*/
/*  varlist_NewSearchContext                                                  */
/*!
    Create a new search context for a client

    The varlist_NewSearchContext function searches through the
    search contexts of the variable server, looking for one which is
    unused.  If none is found, a new one is created.

    The search context contains all the details of the requested search

    @param[in]
        clientPID
            process identifier of the client requesting the search context

    @param[in]
        searchType
            integer specifing the search type, containing one or more of
            the following flags:
               QUERY_REGEX
               QUERY_MATCH
               QUERY_FLAGS
               QUERY_TAGS
               QUERY_INSTANCEID

    @param[in]
        pVarInfo
            pointer to the variable info to search for

    @param[in]
        searchText
            pointer to the regex search text

    @retval pointer to the created search context
    @retval NULL if the requested search context does not exist

==============================================================================*/
static SearchContext *varlist_NewSearchContext( pid_t clientPID,
                                                int searchType,
                                                VarInfo *pVarInfo,
                                                char *searchText )
{
    SearchContext **pp = &pSearchContexts;
    SearchContext *p = NULL;

    /* find an unused pre-existing context */
    while( *pp != NULL )
    {
        if( (*pp)->contextId == 0 )
        {
            p = *pp;
            break;
        }

        pp = &((*pp)->pNext);
    }

    if ( p == NULL )
    {
        /* allocate a new search context */
        p = calloc( 1, sizeof( SearchContext ) );
        *pp = p;
    }

    /* populate the search context */
    if( p != NULL )
    {
        ++contextIdent;
        p->contextId = contextIdent;
        p->clientPID = clientPID;
        p->query.flags = pVarInfo->flags;
        p->query.type = searchType;
        memcpy(&(p->query.tagspec), &(pVarInfo->tagspec), MAX_TAGSPEC_LEN );
        p->query.match = strdup( searchText );
    }

    return p;
}

/*============================================================================*/
/*  varlist_DeleteSearchContext                                               */
/*!
    Delete a search context which is no longer needed

    The varlist_DeleteSearchContext function clears any resources
    used by the search context and makes it available for use
    by a new client

    @param[in]
        ctx
            pointer to the search context to delete

    @retval EOK search context deleted
    @retval EINVAL invalid arguments

==============================================================================*/
static int varlist_DeleteSearchContext( SearchContext *ctx )
{
    int result = EINVAL;

    if( ctx != NULL )
    {
        if( ctx->query.match != NULL )
        {
            free( ctx->query.match );
            ctx->query.match = NULL;
        }

        ctx->query.flags = 0;
        memset( ctx->query.tagspec, 0, MAX_TAGSPEC_LEN );
        ctx->query.type = 0;
        ctx->contextId = 0;
        ctx->clientPID = -1;

        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  varlist_FindSearchContext                                                 */
/*!
    Find the specified search context for the client

    The varlist_FindSearchContext function searches through the
    search contexts of the variable server, looking for one which is
    owned by the specified client, with the specified context identifier

    @param[in]
        clientPID
            process identifier of the client requesting the search context

    @param[in]
        context
            identifier of the search context to find

    @retval pointer to the requested search context
    @retval NULL if the requested search context does not exist

==============================================================================*/
static SearchContext *varlist_FindSearchContext( pid_t clientPID,
                                                 int context )
{
    SearchContext *p = pSearchContexts;

    while( p != NULL )
    {
        if( ( p->contextId == context ) &&
            ( p->clientPID == clientPID ) )
        {
            /* found it */
            break;
        }

        p = p->pNext;
    }

    return p;
}

/*============================================================================*/
/*  varlist_Match                                                             */
/*!
    Match a variable against query parameters

    The varlist_Match function checks if the specified variable
    matches the query conditions in the specified search context

    @param[in]
        hVar
            handle to the variable to match

    @param[in]
        ctx
            search context containing the search parameters

    @retval EOK the variable was successfully matched against the search context
    @retval EINVAL invalid arguments
    @retval ENOENT the variable did not match the search context

==============================================================================*/
static int varlist_Match( VAR_HANDLE hVar, SearchContext *ctx )
{
    int result = EINVAL;
    VarStorage *pVarStorage;
    int searchtype;
    bool found = true;

    if ( ( ctx != NULL ) &&
         ( hVar < VARSERVER_MAX_VARIABLES ) &&
         ( hVar <= varcount ) )
    {
        /* get a pointer to the variable storage for this variable */
        pVarStorage = &varstore[hVar];

        searchtype = ctx->query.type;

        /* dont find hidden variables in vars queries */
        if( pVarStorage->flags & VARFLAG_HIDDEN )
        {
            found = false;
        }

        /* name matching */
        if( searchtype & QUERY_MATCH )
        {
            found &= (strcasestr(pVarStorage->name, ctx->query.match) != NULL );
        }

        /* instance ID matching */
        if( searchtype & QUERY_INSTANCEID )
        {
            found &= (ctx->query.instanceID == pVarStorage->instanceID );
        }

        /* any flags matching */
        if( searchtype & QUERY_FLAGS )
        {
            found &= (ctx->query.flags & pVarStorage->flags );
        }

        result = ( found == true ) ? EOK : ENOENT;
    }

    return result;
}

/*! @}
 * end of varlist group */
