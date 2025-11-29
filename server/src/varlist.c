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
#include <inttypes.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <ctype.h>
#include <regex.h>
#include <varserver/varclient.h>
#include <varserver/varserver.h>
#include <varserver/varobject.h>
#include <varserver/var.h>
#include "server.h"
#include "varlist.h"
#include "taglist.h"
#include "notify.h"
#include "blocklist.h"
#include "transaction.h"
#include "hash.h"

/*==============================================================================
        Private definitions
==============================================================================*/

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

    /*! variable tag specifiers */
    uint16_t tags[MAX_TAGS_LEN];

    /*! pointer to the next search context */
    struct _searchContext *pNext;
} SearchContext;

/*! Variable Alias Reference */
typedef struct _VarAlias
{
    /*! pointer to the alias name */
    struct _var_id *pVarID;

    /*! pointer to the next alias name */
    struct _VarAlias *pNext;
} VarAlias;

/*! The VarStorage object is used internally by the varserver to
    encapsulate all the information about a single variable */
typedef struct _VarStorage
{
    /*! reference counter counts the number of variables
        associated with this storage */
    uint16_t refCount;

    /*! storage reference id */
    uint32_t storageRef;

    /*! variable data */
    VarObject var;

    /*! variable flags */
    uint32_t flags;

    /*! variable tag specifiers */
    uint16_t tags[MAX_TAGS_LEN];

    /*! variable format specifier */
    char formatspec[MAX_FORMATSPEC_LEN];

    /*! variable permissions */
    VarPermissions permissions;

    /*! pointer to the notification list for this variable */
    Notification *pNotifications;

    /* indicate if this variable has an associated CALC notification */
    uint16_t notifyMask;

    /*! list of aliases */
    VarAlias *pAliases;

} VarStorage;

/*! Variable Identifier */
typedef struct _var_id
{
    /*! handle to the variable */
    VAR_HANDLE hVar;

    /*! instance identifier for this variable */
    uint32_t instanceID;

    /*! name of the variable */
    char name[MAX_NAME_LEN+1];

    /*! globally unique identifier for the variable */
    uint32_t guid;

    /* pointer to the VarStorage object associated with this VarID */
    VarStorage *pVarStorage;

} VarID;

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! counts the number of variables in the list */
static int varcount = 0;

/*! variable storage */
static VarID varstore[ VARSERVER_MAX_VARIABLES ] = {0};

/*! search contexts */
static SearchContext *pSearchContexts = NULL;

/*! unique context identifier */
static int contextIdent = 0;

/*! id of user that started varserver */
static uid_t varserver_uid;

/*! Number of VarStorage objects we have created */
static uint32_t NumVarStorage = 0;

/*==============================================================================
        Private function declarations
==============================================================================*/

static int AssignVarInfo( VarID *pVarID,
                          VarStorage *pVarStorage,
                          VarInfo *pVarInfo );
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
static int varlist_CalcError( VarClient *pVarClient, void *arg );

static int varlist_SendNotifications( pid_t clientPID,
                                      VarStorage *pVarStorage,
                                      VAR_HANDLE hVar );

static int varlist_HandleTrigger( pid_t clientPID,
                                  VarStorage *pVarStorage,
                                  VAR_HANDLE hVar );

static SearchContext *varlist_NewSearchContext( pid_t clientPID,
                                                int searchType,
                                                VarInfo *pVarInfo,
                                                char *searchText );
static int varlist_DeleteSearchContext( SearchContext *ctx );
static SearchContext *varlist_FindSearchContext( pid_t clientPID,
                                                 int context );

static int varlist_Match( VarID *pVarID, SearchContext *ctx );
static int varlist_MatchTags( uint16_t *pHaystack, uint16_t *pNeedle );

static int assign_BlobVarInfo( VarStorage *pVarStorage, VarInfo *pVarInfo );
static int assign_StringVarInfo( VarStorage *pVarStorage, VarInfo *pVarInfo );

static int varlist_CopyVarInfoBlobToClient( VarClient *pVarClient,
                                            VarInfo *pVarInfo );
static int varlist_CopyVarInfoStringToClient( VarClient *pVarClient,
                                              VarInfo *pVarInfo );

static int varlist_CopyVarStorageBlobToClient( VarClient *pVarClient,
                                               VarStorage *pVarStorage );

static int varlist_CopyVarStorageStringToClient( VarClient *pVarClient,
                                                 VarStorage *pVarStorage );

static void *varlist_GetNotificationPayload( VAR_HANDLE hVar,
                                             VarStorage *pVarStorage,
                                             size_t *size );

static void varlist_SetDirty( VarStorage *pVarStorage );

static bool varlist_CheckReadPermissions( VarInfo *pVarInfo,
                                          VarID *pVarID );

static bool varlist_CheckWritePermissions( VarInfo *pVarInfo,
                                           VarID *pVarStorage );

static int varlist_HandleMetric( VarInfo *pVarInfo,
                                 VarStorage *pVarStorage );

static int varlist_Audit( pid_t clientPID,
                          VarID *pVarID,
                          VarInfo *pVarInfo );

static VarID *varlist_FindVar( VarInfo *pVarInfo );

static VarID *varlist_GetVarID( VarInfo *pVarInfo );

static int varlist_NewAlias( VarInfo *pVarInfo,
                             VarID *pVarID,
                             uint32_t *pVarHandle );

static int varlist_SelfAlias( VarID *pVarID );

static int varlist_MoveAlias( VarID *pAliasID,
                              VarID *pVarID,
                              uint32_t *pVarHandle );

static VarAlias *varlist_DeleteAliasReference( VarID *pVarID,
                                               VarStorage *pVarStorage );

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
    VarStorage *pVarStorage;
    VarID *pVarID;
    char buf[256];
    char *name;

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
                pVarID = &varstore[varhandle];
                pVarStorage = calloc( 1, sizeof( VarStorage ) );
                if ( pVarStorage != NULL )
                {
                    /* set the storage reference */
                    pVarStorage->storageRef = ++NumVarStorage;

                    /* set the variable storage pointer */
                    pVarID->pVarStorage = pVarStorage;

                    /* construct the fully qualified variable name */
                    name = VARLIST_FQN( pVarInfo, buf, sizeof( buf ) );

                    /* add the VarStorage object to the Hash Table */
                    result = HASH_Add( name, pVarID );
                    if ( result == EOK )
                    {
                        /* copy the variable information from the VarInfo object
                        to the VarStorage object */
                        result = AssignVarInfo( pVarID, pVarStorage, pVarInfo );
                        if( result == EOK )
                        {
                            /* increment the number of variables */
                            varcount++;

                            /* increment the storage reference counter */
                            pVarStorage->refCount = 1;

                            /* set the variable handle */
                            pVarID->hVar = varhandle;

                            /* assign the variable handle */
                            *pVarHandle = varhandle;
                        }
                    }
                }
                else
                {
                    result = ENOMEM;
                }
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
/*  VARLIST_Alias                                                             */
/*!
    Create a variable alias for an existing variable

    The VARLIST_Alias function creates a new variable alias for an existing
    variable or moves an existing alias to a new variable.

    @param[in]
        pVarInfo
            Pointer to the new variable to add

    @param[out]
        pVarHandle
            pointer to a location to store the handle to the new variable

    @retval EOK the new alias was create
    @retval ENOENT the variable to be aliased does not exist
    @retval EEXIST the variable already exists
    @retval ENOMEM memory allocation failure
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_Alias( VarInfo *pVarInfo, uint32_t *pVarHandle )
{
    int result = EINVAL;
    VarID *pAliasVarID;
    VarID *pVarID;

    if ( pVarInfo != NULL )
    {
        /* find the variable to be aliased and confirm we have
           access to read it */
        pVarID = varlist_GetVarID( pVarInfo );
        if ( varlist_CheckReadPermissions( pVarInfo, pVarID ) == true )
        {
            /* see if the alias already exists */
            pAliasVarID = varlist_FindVar( pVarInfo );
            if ( pAliasVarID != NULL )
            {
                /* confirm we have read access to the alias too */
                if ( varlist_CheckReadPermissions( pVarInfo,
                                                   pAliasVarID ) == true )
                {
                    /* move the alias to the new variable */
                    result = varlist_MoveAlias( pAliasVarID,
                                                pVarID,
                                                pVarHandle );
                }
                else
                {
                    /* we do not have permission to read the alias variable */
                    result = ENOENT;
                }
            }
            else
            {
                /* create a new alias to the variable */
                result = varlist_NewAlias( pVarInfo,
                                           pVarID,
                                           pVarHandle );
            }
        }
        else
        {
            /* cannot find variable to be aliased */
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  varlist_SelfAlias                                                         */
/*!
    Create a self-alias for a variable

    The varlist_SelfAlias function creates a self-alias for the specified
    variable.  This is necessary for reporting functions which list
    all the aliases of a variable.

    @param[in]
        pVarID
            Pointer to the variable identifier which is to be self-aliased

    @retval EOK the new alias was create
    @retval ENOENT the variable to be aliased does not exist
    @retval EEXIST the variable already exists
    @retval ENOMEM memory allocation failure
    @retval EINVAL invalid arguments

==============================================================================*/
static int varlist_SelfAlias( VarID *pVarID )
{
    int result = EINVAL;
    VarStorage *pVarStorage;
    VarAlias *pSelfAlias;

    if ( pVarID != NULL )
    {
        /* get a pointer to the variable storage */
        pVarStorage = pVarID->pVarStorage;
        if ( pVarStorage != NULL )
        {
            /* create a VarAlias object */
            pSelfAlias = malloc( sizeof( VarAlias ) );
            if ( pSelfAlias != NULL )
            {
                /* populate the VarAlias object */
                pSelfAlias->pVarID = pVarID;
                pSelfAlias->pNext = pVarStorage->pAliases;

                /* insert the VarAlias object at the head of the alias list */
                pVarStorage->pAliases = pSelfAlias;

                /* success */
                result = EOK;
            }
            else
            {
                /* cannot allocate memory for the alias object */
                result = ENOMEM;
            }
        }
        else
        {
            /* no storage associated with the variable identifier */
            result = ENOTSUP;
        }
    }

    return result;
}

/*============================================================================*/
/*  varlist_NewAlias                                                          */
/*!
    Create a new alias for the specified variable

    The varlist_NewAlias function creates a new variable alias for an existing
    variable.

    @param[in]
        pVarInfo
            Pointer to the new variable to add containing the
            name, instance ID, and credentials.

    @param[in]
        pVarID
            pointer to the existing variable to alias

    @param[out]
        pVarHandle
            pointer to a location to store the handle to the new variable

    @retval EOK the new alias was create
    @retval ENOENT the variable to be aliased does not exist
    @retval EEXIST the variable already exists
    @retval ENOMEM memory allocation failure
    @retval EINVAL invalid arguments

==============================================================================*/
static int varlist_NewAlias( VarInfo *pVarInfo,
                             VarID *pVarID,
                             uint32_t *pVarHandle )
{
    int result = EINVAL;
    int varhandle;
    VarStorage *pVarStorage;
    VarID *pAliasVarID;
    char buf[256];
    char *name;
    VarAlias *pVarAlias;

    if ( ( pVarID != NULL ) &&
         ( pVarInfo != NULL ) )
    {
        /* get the storage location for the variable */
        pVarStorage = pVarID->pVarStorage;
        if ( pVarStorage != NULL )
        {
            /* check if we have enough space for another variable */
            if( varcount < VARSERVER_MAX_VARIABLES )
            {
                /* create a VarAlias object */
                pVarAlias = malloc( sizeof( VarAlias ));
                if ( pVarAlias != NULL )
                {
                    /* get a pointer to the alias Variable identifier */
                    varhandle = ++varcount;
                    pAliasVarID = &varstore[varhandle];

                    /* copy the variable name */
                    strncpy( pAliasVarID->name,
                            pVarInfo->name,
                            MAX_NAME_LEN );
                    pAliasVarID->name[MAX_NAME_LEN-1] = 0;

                    /* construct the fully qualified variable name */
                    name = VARLIST_FQN( pVarInfo, buf, sizeof( buf ) );

                    /* populate the VarID data */
                    pAliasVarID->guid = pVarInfo->guid;
                    pAliasVarID->hVar = varhandle;
                    pAliasVarID->instanceID = pVarInfo->instanceID;
                    pAliasVarID->pVarStorage = pVarStorage;

                    pVarStorage->refCount++;
                    if ( pVarStorage->refCount > 1 )
                    {
                        /* we have more than one reference, so
                            set the alias flag on this storage */
                        pVarStorage->flags |= VARFLAG_ALIAS;
                    }

                    /* return the storage reference identifier */
                    pVarInfo->storageRef = pVarStorage->storageRef;

                    if ( pVarHandle != NULL )
                    {
                        /* return the new variable handle */
                        *pVarHandle = pAliasVarID->hVar;
                    }

                    /* attach self alias if this is the first alias for
                       the variable */
                    if ( pVarStorage->pAliases == NULL )
                    {
                        varlist_SelfAlias( pVarID );
                    }

                    /* attach the variable alias to the variable storage */
                    pVarAlias->pVarID = pAliasVarID;
                    pVarAlias->pNext = pVarStorage->pAliases;
                    pVarStorage->pAliases = pVarAlias;

                    /* add the Alias VarID object to the Hash Table */
                    result = HASH_Add( name, pAliasVarID );
                }
                else
                {
                    /* no memory for the VarAlias object */
                    result = ENOMEM;
                }
            }
            else
            {
                /* no space for more variables */
                result = ENOMEM;
            }
        }
        else
        {
            /* no storage object associated with the variable id */
            result = ENOMEM;
        }
    }

    return result;
}

/*============================================================================*/
/*  varlist_MoveAlias                                                         */
/*!
    Move an alias to a new variable

    The varlist_MoveAlias function moves an alias from one variable
    to another.  In order to complete the move, we have to also move
    any notifications from the old variable (the one losing the alias) to
    the new variable (the one gaining the alias).  The move can only
    be completed if the new variable does not already have any CALC,
    VALIDATE, or PRINT notifications which would be replaced by those
    coming with the alias.

    @param[in]
        pAliasID
            Pointer to the VarID object associated with the alias to be moved

    @param[in]
        pVarID
            pointer to the VarID object of the destination variable

    @param[in,out]
        pVarHandle
            pointer to a location to store the handle of the alias variable

    @retval EOK the alias was moved
    @retval ENOENT the variable to be aliased does not exist
    @retval ENOTSUP the alias cannot be moved due to a conflict
    @retval EINVAL invalid arguments

==============================================================================*/
static int varlist_MoveAlias( VarID *pAliasID,
                              VarID *pVarID,
                              uint32_t *pVarHandle )
{
    int result = EINVAL;
    VarStorage *pVarStorage;
    VarStorage *pAliasStorage;
    VarAlias *pVarAlias;

    if ( ( pAliasID != NULL ) &&
         ( pVarID != NULL ) &&
         ( pAliasID->pVarStorage != NULL ) &&
         ( pVarID->pVarStorage != NULL ) )
    {
        pVarStorage = pVarID->pVarStorage;
        pAliasStorage = pAliasID->pVarStorage;

        if ( ( pAliasID == pVarID ) ||
             ( pVarStorage == pAliasStorage ) ||
             ( pAliasStorage->refCount == 1 ) )
        {
            /* we cannot move an alias to itself */
            /* we cannot move an alias if it is the last reference to a var */
            result = ENOTSUP;
        }
        else
        {
            /* check if we can move the notifications */
            result = NOTIFY_CheckMove( pAliasID->hVar,
                                       pAliasStorage->pNotifications,
                                       pVarStorage->pNotifications );
            if ( result == EOK )
            {
                /* move the alias notifications to the target variable */
                result = NOTIFY_Move( pAliasID->hVar,
                                      &(pAliasStorage->pNotifications),
                                      &(pVarStorage->pNotifications) );
                if ( result == EOK )
                {
                    /* delete the alias reference from its current variable  */
                    pVarAlias = varlist_DeleteAliasReference(
                                                    pAliasID,
                                                    pAliasID->pVarStorage );
                    if ( pVarAlias != NULL )
                    {
                        if ( pVarStorage->pAliases == NULL )
                        {
                            /* if this is the first alias for the target
                               variable, create a self alias for the target
                               variable. */
                            varlist_SelfAlias( pVarID );
                        }

                        /* store the variable alias in the variable storage */
                        pVarAlias->pNext = pVarStorage->pAliases;
                        pVarStorage->pAliases = pVarAlias;
                    }

                    /* recalculate the notification list masks */
                    pAliasStorage->notifyMask =
                        NOTIFY_GetMask( pAliasStorage->pNotifications );

                    pVarStorage->notifyMask =
                        NOTIFY_GetMask( pVarStorage->pNotifications );

                    /* update the data storage pointer for the alias */
                    pAliasID->pVarStorage = pVarStorage;

                    /* decrement the reference count on the previously
                       aliased variable and clear the alias flag if
                       it has no more aliases */
                    pAliasStorage->refCount--;
                    if ( pAliasStorage->refCount <= 1 )
                    {
                        pAliasStorage->flags &= ~VARFLAG_ALIAS;
                    }

                    /* increment the reference count on the newly
                       aliased variable and set the alias flag
                       if it has more than one reference */
                    pVarStorage->refCount++;
                    if ( pVarStorage->refCount > 1 )
                    {
                        pVarStorage->flags |= VARFLAG_ALIAS;
                    }

                    if ( pVarHandle != NULL )
                    {
                        /* store the handle to the alias variable */
                        *pVarHandle = pAliasID->hVar;
                    }
                }
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  varlist_DeleteAliasReference                                              */
/*!
    Delete the Alias Reference from the storage

    The varlist_DeleteAliasReference function deletes the alias reference
    from the variable storage.

    @param[in]
        pVarID
            Pointer to the VarID object associated with the alias to be deleted

    @param[in]
        pVarStorage
            pointer to the VarStorage object to delete the alias from

    @retval pointer to the VarAlias which was removed
    @retval NULL if the Variable alias was not found

==============================================================================*/
static VarAlias *varlist_DeleteAliasReference( VarID *pVarID,
                                               VarStorage *pVarStorage )
{
    VarAlias **ppVarAlias;
    VarAlias *p = NULL;

    if ( ( pVarID != NULL ) &&
         ( pVarStorage != NULL ) )
    {
        ppVarAlias = &(pVarStorage->pAliases);

        p = pVarStorage->pAliases;

        while( p != NULL )
        {
            if ( p->pVarID == pVarID )
            {
                /* update the previous alias pointer to skip the
                   alias we are deleting */
                *ppVarAlias = p->pNext;

                /* done, we can exit */
                break;
            }
            else
            {
                /* update the alias pointer reference */
                ppVarAlias = &(p->pNext);
            }

            /* move to the next alias */
            p = p->pNext;
        }
    }

    return p;
}

/*============================================================================*/
/*  varlist_FindVar                                                           */
/*!
    Find a variable object given its name and instance ID

    The varlist_FindVar function finds the VarID object
    in the Hash Table given its name and instance identifier.

    @param[in]
        pVarInfo
            Pointer to the variable definition containing the name and
            instance identifier of the variable to find

    @retval pointer to the VarID object found
    @retval NULL if no VarStorage object was found

==============================================================================*/
static VarID *varlist_FindVar( VarInfo *pVarInfo )
{
    VarID *pVarID = NULL;
    char buf[256];
    char *name;

    if( pVarInfo != NULL )
    {
        /* get the variable's fully qualified name */
        name = VARLIST_FQN( pVarInfo, buf, sizeof( buf ) );

        /* find the VarStorage given its name */
        pVarID = HASH_Find( name );
    }

    return pVarID;
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
    VarID *pVarID;

    if( ( pVarInfo != NULL ) &&
        ( pVarHandle != NULL ) )
    {
        /* assume we didn't find it */
        result = ENOENT;

        /* store VAR_INVALID in the variable handle */
        *pVarHandle = VAR_INVALID;

        /* find the VarID object */
        pVarID = varlist_FindVar( pVarInfo );
        if ( pVarID != NULL )
        {
            /* check if we have read permissions on the variable */
            if ( varlist_CheckReadPermissions( pVarInfo, pVarID ))
            {
                *pVarHandle = pVarID->hVar;
                result = EOK;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  VARLIST_Exists                                                            */
/*!
    Check if a variable exists

    The VARLIST_Exists function searches the variable list to see if the
    variable with the specified name and instance identifier exists.

    @param[in]
        pVarInfo
            Pointer to the variable definition containing the name and
            instance identifier of the variable to check

    @retval EOK the variable exists
    @retval ENOENT the variable does not exist
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_Exists( VarInfo *pVarInfo )
{
    int result = EINVAL;

    if ( pVarInfo != NULL )
    {
        result = ( varlist_FindVar( pVarInfo ) != NULL ) ? EOK : ENOENT;
    }

    return result;
}

/*============================================================================*/
/*  VARLIST_FQN                                                               */
/*!
    Construct a Fully Qualified Name (FQN) for the specified variable

    The VARLIST_FQN function constructs a fully qualified name from the
    specified variable info using its name and instance identifier.
    The name is converted to lower case

    @param[in]
        pVarInfo
            Pointer to the variable definition containing the name and
            instance identifier of the variable

    @param[in,out]
        buf
            Pointer to the output buffer to write the fully qualified name into

    @param[in]
        len
            specifies the length of the output buffer

    @retval pointer to the Fully Qualified Name
    @retval NULL if the Fully qualified name could not be constructed

==============================================================================*/
char *VARLIST_FQN( VarInfo *pVarInfo, char *buf, size_t len )
{
    char *p = NULL;
    int i = 0;
    int j = 0;
    char *name;

    if ( ( pVarInfo != NULL ) &&
         ( buf != NULL ) &&
         ( len > 0 ) )
    {
        name = pVarInfo->name;

        /* prepend the name with the instance identifier */
        if ( pVarInfo->instanceID != 0 )
        {
            /* construct the fully qualified name */
            i = snprintf( buf,
                          len,
                          "[%" PRIu32 "]",
                          pVarInfo->instanceID );
            len -= i;
        }

        /* copy the name and map to lower case */
        while ( ( (size_t)i < len ) && ( name[j] != 0 ) )
        {
            buf[i++] = tolower( name[j++] );
        }

        if ( (size_t)i < len )
        {
            /* NUL terminate */
            buf[i] = 0;
            p = buf;
        }
    }

    return p;
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
    VarID *pVarID;
    VarStorage *pVarStorage = NULL;
    int result = EINVAL;
    VAR_HANDLE hVar = VAR_INVALID;
    VAR_HANDLE hTransactionVar = VAR_INVALID;
    size_t n;
    uint32_t printHandle;
    char *p;
    Notification *pNotifications;

    if( ( pVarInfo != NULL ) &&
        ( workbuf != NULL ) )
    {
        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            pVarStorage = pVarID->pVarStorage;
            hVar = pVarID->hVar;
        }

        if( ( pVarStorage != NULL ) &&
            ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
        {
            /* get the storage reference identifier */
            pVarInfo->storageRef = pVarStorage->storageRef;

            /* check if this variable has a PRINT handler attached */
            if( pVarStorage->notifyMask & NOTIFY_MASK_PRINT )
            {
                /* get the handle associated with the PRINT notification */
                pNotifications = pVarStorage->pNotifications;
                hTransactionVar = NOTIFY_GetVarHandle( pNotifications,
                                                       NOTIFY_PRINT );

                /* create a PRINT transaction */
                result = TRANSACTION_New( clientPID,
                                          clientInfo,
                                          hTransactionVar,
                                          &printHandle );
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
                        pVarStorage->notifyMask |=
                            NOTIFY_MASK_HAS_PRINT_BLOCK;

                        /* indicate that the print output will be piped
                        from another client */
                        result = ESTRPIPE;

                        return result;
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

                /* returning the value if it is not a password */
                if ( ! ( pVarStorage->flags & VARFLAG_PASSWORD ) )
                {
                    pVarInfo->var.val = pVarStorage->var.val;
                }

                /* get the format specifier */
                memcpy( pVarInfo->formatspec,
                        pVarStorage->formatspec,
                        MAX_FORMATSPEC_LEN );

                /* get the flags */
                pVarInfo->flags = pVarStorage->flags;

                if( pVarInfo->var.type == VARTYPE_STR )
                {
                    if ( pVarStorage->flags & VARFLAG_PASSWORD )
                    {
                        p = "********";
                        n = strlen( p );
                    }
                    else
                    {
                        p = pVarStorage->var.val.str;
                        n = strlen(p);
                    }

                    if ( p != NULL )
                    {
                        /* strings are passed back via the working buffer */
                        if( n < workbufsize )
                        {
                            /* copy the string */
                            memcpy( workbuf, p, n );
                            workbuf[n] = '\0';
                        }
                        else
                        {
                            result = E2BIG;
                        }
                    }
                    else
                    {
                        workbuf[0] = 0;
                    }
                }

                if( pVarInfo->var.type == VARTYPE_BLOB )
                {
                    /* blobs are passed back via the working buffer */
                    n = pVarStorage->var.len;
                    if( n <= workbufsize )
                    {
                        /* copy the blob */
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
    VarStorage *pVarStorage = NULL;
    VarID *pVarID;
    int result = EINVAL;
    VAR_HANDLE hVar = VAR_INVALID;
    size_t n;

    if( ( pVarInfo != NULL ) &&
        ( buf != NULL ) )
    {
        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            pVarStorage = pVarID->pVarStorage;
            hVar = pVarID->hVar;
        }

        if ( ( pVarStorage != NULL ) &&
             ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
        {
            /* get the storage reference identifier */
            pVarInfo->storageRef = pVarStorage->storageRef;

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

    @param[in]
        clientPID
            the process ID of the client setting the variable

    @param[in,out]
        pVarInfo
            Pointer to the variable definition containing the handle
            of the variable to set and the value to set it to

    @param[in,out]
        validationInProgress
            pointer to the location to store the validationInProgress flag

    @param[in]
        clientInfo
            opaque pointer to the client VarClient object

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
    VarID *pVarID;
    VarStorage *pVarStorage = NULL;
    VAR_HANDLE hVar = VAR_INVALID;
    uint32_t validateHandle;
    Notification *pNotifications;
    VAR_HANDLE hTransactionVar = VAR_INVALID;
    int rc;

    if( pVarInfo != NULL )
    {
        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            pVarStorage = pVarID->pVarStorage;
            hVar = pVarID->hVar;
        }

        if ( ( pVarStorage != NULL ) &&
             ( validationInProgress != NULL ) &&
             ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
        {
            /* check if this variable is readonly */
            if ( pVarStorage->flags & VARFLAG_READONLY )
            {
                return EACCES;
            }

            /* check if we have write permissions on this variable */
            rc = varlist_CheckWritePermissions( pVarInfo, pVarID );
            if ( rc == false )
            {
                return EACCES;
            }

            /* get the storage reference identifier */
            pVarInfo->storageRef = pVarStorage->storageRef;

            /* handle metric counter operations */
            rc = varlist_HandleMetric( pVarInfo, pVarStorage );
            if ( rc != EOK )
            {
                return rc;
            }

            /* handle trigger notifications */
            rc = varlist_HandleTrigger( clientPID, pVarStorage, hVar );
            if ( rc == EOK )
            {
                /* further processing disabled for trigger variables */
                return EOK;
            }

            /* Check if we have a validation handler on this variable */
            if( ( pVarStorage->notifyMask & NOTIFY_MASK_VALIDATE ) &&
                ( *validationInProgress == false ) )
            {
                /* prevent self-notification */
                if( NOTIFY_Find( pVarStorage->pNotifications,
                                 NOTIFY_VALIDATE,
                                 clientPID ) == NULL )
                {
                    /* get the handle associated with the PRINT notification */
                    pNotifications = pVarStorage->pNotifications;
                    hTransactionVar = NOTIFY_GetVarHandle( pNotifications,
                                                           NOTIFY_VALIDATE );

                    /* create a validation transaction */
                    result = TRANSACTION_New( clientPID,
                                              clientInfo,
                                              hTransactionVar,
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

            if ( result == EOK )
            {
                varlist_SetDirty( pVarStorage );
            }

            if ( pVarStorage->flags & VARFLAG_AUDIT )
            {
                varlist_Audit( clientPID, pVarID, pVarInfo );
            }

            if ( ( result == EOK ) ||
                 ( result == EALREADY ) )
            {
                /* check for any CALC blocked clients on the variable */
                if( pVarStorage->notifyMask & NOTIFY_MASK_HAS_CALC_BLOCK )
                {
                    /* unblock the first CALC blocked client */
                    UnblockClients( pVarStorage->storageRef,
                                    NOTIFY_CALC,
                                    varlist_Calc,
                                    (void *)pVarInfo );

                    /* indicate we no longer have CALC blocked clients */
                    pVarStorage->notifyMask &= ~NOTIFY_MASK_HAS_CALC_BLOCK;
                }

                if ( result == EOK )
                {
                    /* send out notifications */
                    result = varlist_SendNotifications( clientPID,
                                                        pVarStorage,
                                                        hVar );
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

/*============================================================================*/
/*  VARLIST_CalcResponse                                                      */
/*!
    Handle a CALC response from a client

    The VARLIST_CalcResponse function handles a CALC response from a client.
    It is used to unblock the requesting client and pass an error message
    when the calc cannot be completed successfully.

    @param[in]
        clientPID
            the process ID of the responding client

    @param[in]
        pVarInfo
            Pointer to the variable definition containing the handle
            of the variable containing the notification list
            of the clients to notify.

    @param[in]
        clientInfo
            opaque pointer to the client VarClient object

    @retval EOK the calc response was successfully handled
    @retval ENOENT the variable does not exist
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_CalcResponse( pid_t clientPID,
                          VarInfo *pVarInfo,
                          void *clientInfo )
{
    int result = EINVAL;
    VarID *pVarID;
    VarStorage *pVarStorage = NULL;

    if( ( pVarInfo != NULL ) &&
        ( clientInfo != NULL ) )
    {
        /* get the variable handle and storage */
        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            pVarStorage = pVarID->pVarStorage;
        }

        /* check if the variable is found and is not read-only */
        if ( ( pVarStorage != NULL ) &&
             ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
        {
            /* check if the requesting client has access to send the
               calc response */
            if ( ( pVarStorage->flags & VARFLAG_READONLY ) ||
                 ( varlist_CheckWritePermissions(pVarInfo, pVarID) == false ) ||
                 ( NOTIFY_Find( pVarStorage->pNotifications,
                                NOTIFY_CALC,
                                clientPID ) == NULL ) )
            {
                /* indicate the responding client does not have access
                   to the variable */
                result = EACCES;
            }
            else
            {
                /* check for any CALC blocked clients on the variable */
                if( pVarStorage->notifyMask & NOTIFY_MASK_HAS_CALC_BLOCK )
                {
                    /* unblock the CALC blocked clients */
                    UnblockClients( pVarStorage->storageRef,
                                    NOTIFY_CALC,
                                    varlist_CalcError,
                                    clientInfo );

                    /* indicate we no longer have CALC blocked clients */
                    pVarStorage->notifyMask &= ~NOTIFY_MASK_HAS_CALC_BLOCK;
                }

                /* success */
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

/*============================================================================*/
/*  varlist_Audit                                                             */
/*!
    Create a variable change audit log

    The varlist_Audit function creates a variable change audit log,
    using syslog, and writes the variable name, value, user id, and
    process id that requested the change.

    @param[in]
        clientPID
            process id of the client requesting the change

    @param[in]
        pVarID
            pointer to the variable ID object that was changed

    @param[in]
        pVarInfo
            pointer to the variable change info for the change request

    @retval EOK the variable audit log was written
    @retval ENOTSUP unsupported data type
    @retval EINVAL invalid arguments

==============================================================================*/
static int varlist_Audit( pid_t clientPID,
                          VarID *pVarID,
                          VarInfo *pVarInfo )
{
    int result = EINVAL;
    VarStorage *pVarStorage = NULL;
    uid_t uid;
    char *varname;

    if ( ( pVarInfo != NULL ) &&
         ( pVarID != NULL ) )
    {
        uid = pVarInfo->creds[0];
        varname = pVarID->name;

        pVarStorage = pVarID->pVarStorage;
        if ( pVarStorage != NULL )
        {
            result = EOK;

            switch( pVarStorage->var.type )
            {
                case VARTYPE_FLOAT:
                    syslog( LOG_INFO,
                            "'%s' changed to '%f' by user %d from process %d",
                            varname,
                            pVarStorage->var.val.f,
                            uid,
                            clientPID  );
                    break;

                case VARTYPE_BLOB:
                    syslog( LOG_INFO,
                            "'%s' changed by user %d from process %d",
                            varname,
                            uid,
                            clientPID  );
                    result = EOK;
                    break;

                case VARTYPE_STR:
                    if ( pVarStorage->var.val.str != NULL )
                    {
                        syslog( LOG_INFO,
                                "'%s' changed to '%s' by user %d "
                                "from process %d",
                                varname,
                                pVarStorage->var.val.str,
                                uid,
                                clientPID  );
                    }
                    result = EOK;
                    break;

                case VARTYPE_UINT16:
                    syslog( LOG_INFO,
                            "'%s' changed to '%u' by user %d from process %d",
                            varname,
                            pVarStorage->var.val.ui,
                            uid,
                            clientPID  );
                    break;

                case VARTYPE_INT16:
                    syslog( LOG_INFO,
                            "'%s' changed to '%d' by user %d from process %d",
                            varname,
                            pVarStorage->var.val.i,
                            uid,
                            clientPID  );
                    break;

                case VARTYPE_UINT32:
                    syslog( LOG_INFO,
                            "'%s' changed to '"
                            "%" PRIu32
                            "' by user %d from process %d",
                            varname,
                            pVarStorage->var.val.ul,
                            uid,
                            clientPID  );
                    break;

                case VARTYPE_INT32:
                    syslog( LOG_INFO,
                            "'%s' changed to '"
                            "%" PRId32
                            "' by user %d from process %d",
                            varname,
                            pVarStorage->var.val.l,
                            uid,
                            clientPID  );
                    break;

                case VARTYPE_UINT64:
                    syslog( LOG_INFO,
                            "'%s' changed to '"
                            "%" PRIu64
                            "' by user %d from process %d",
                            varname,
                            pVarStorage->var.val.ull,
                            uid,
                            clientPID  );
                    break;

                case VARTYPE_INT64:
                    syslog( LOG_INFO,
                            "'%s' changed to '"
                            "%" PRId64
                            "' by user %d from process %d",
                            varname,
                            pVarStorage->var.val.ll,
                            uid,
                            clientPID  );
                    break;

                default:
                    result = ENOTSUP;
                    break;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  VARLIST_SetFlags                                                          */
/*!
    Handle a SET_FLAGS request from a client

    The VARLIST_SetFlags function handles a SET_FLAGS request from a client.
    It sets the specified flags on the specified variable

    @param[in]
        pVarInfo
            Pointer to the variable definition containing the handle
            of the variable whose flags will be set

    @retval EOK the variable flags were successfully set
    @retval ENOENT the variable does not exist
    @retval EACCES insufficient permissions to set the flags
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_SetFlags( VarInfo *pVarInfo )
{
    int result = EINVAL;
    VarStorage *pVarStorage = NULL;
    VarID *pVarID;
    bool rc;

    if( pVarInfo != NULL )
    {
        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            pVarStorage = pVarID->pVarStorage;
        }

        result = ENOENT;

        if ( ( pVarStorage != NULL ) &&
             ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
        {
            /* check if we have write permissions on this variable */
            rc = varlist_CheckWritePermissions( pVarInfo, pVarID );
            if ( rc == true )
            {
                pVarStorage->flags |= pVarInfo->flags;

                /* get the storage reference identifier */
                pVarInfo->storageRef = pVarStorage->storageRef;

                result = EOK;
            }
            else
            {
                result = EACCES;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  VARLIST_ClearFlags                                                        */
/*!
    Handle a CLEAR_FLAGS request from a client

    The VARLIST_ClearFlags function handles a CLEAR_FLAGS request from a client.
    It clears the specified flags on the specified variable

    @param[in]
        pVarInfo
            Pointer to the variable definition containing the handle
            of the variable whose flags will be cleared

    @retval EOK the variable flags were successfully cleared
    @retval ENOENT the variable does not exist
    @retval EACCES insufficient permissions to clear the flags
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_ClearFlags( VarInfo *pVarInfo )
{
    int result = EINVAL;
    VarStorage *pVarStorage = NULL;
    VarID *pVarID;
    int rc;

    if( pVarInfo != NULL )
    {
        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            pVarStorage = pVarID->pVarStorage;
        }

        result = ENOENT;

        if( ( pVarStorage != NULL ) &&
            ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
        {
            /* check if we have write permissions on this variable */
            rc = varlist_CheckWritePermissions( pVarInfo, pVarID );
            if ( rc == true )
            {
                pVarStorage->flags &= ~(pVarInfo->flags);

                /* get the storage reference identifier */
                pVarInfo->storageRef = pVarStorage->storageRef;

                result = EOK;
            }
            else
            {
                result = EACCES;
            }
        }
    }

    return result;
}

/*==============================================================================
        Private function definitions
==============================================================================*/

/*============================================================================*/
/*  varlist_SendNotifications                                                 */
/*!
    Send out requested client notifications

    The varlist_SendNotifications function sends out requested client
    notifications of type modified and modified_queued.

    @param[in]
        clientPID
            process identifier of the requesting client

    @param[in]
        pVarStorage
            pointer to the variable storage for the variable notification

    @param[in]
        hVar
            handle to the variable that has been modified

    @retval EOK the notifications were sent
    @retval EINVAL invalid arguments

==============================================================================*/
static int varlist_SendNotifications( pid_t clientPID,
                                      VarStorage *pVarStorage,
                                      VAR_HANDLE hVar )
{
    int result = EINVAL;
    void *payload;
    size_t n = 0;

    if ( pVarStorage != NULL )
    {
        result = EOK;

        /* signal variable modified */
        if ( pVarStorage->notifyMask & NOTIFY_MASK_MODIFIED )
        {
            NOTIFY_Signal( clientPID,
                           &pVarStorage->pNotifications,
                           NOTIFY_MODIFIED,
                           hVar,
                           NULL );
        }

        if ( pVarStorage->notifyMask & NOTIFY_MASK_MODIFIED_QUEUE )
        {
            /* prepare the notification payload */
            payload = varlist_GetNotificationPayload( hVar,
                                                      pVarStorage,
                                                      &n );

            /* send the notification payloads */
            NOTIFY_Payload( &pVarStorage->pNotifications,
                            payload,
                            n );

            /* send notification signals to the clients */
            NOTIFY_Signal( clientPID,
                            &pVarStorage->pNotifications,
                            NOTIFY_MODIFIED_QUEUE,
                            hVar,
                            NULL );
        }
    }

    return result;
}

/*============================================================================*/
/*  varlist_HandleTrigger                                                     */
/*!
    Check for and handle modification for a trigger variable

    The varlist_HandleTrigger function checks to see if the variable
    which is being set is a trigger variable.  If it is, and notifications,
    are requested for this variable, then the notification will be sent out.


    @param[in]
        pVarStorage
            Pointer to the variable storage

==============================================================================*/
static int varlist_HandleTrigger( pid_t clientPID,
                                  VarStorage *pVarStorage,
                                  VAR_HANDLE hVar )
{
    int result = EINVAL;

    if ( pVarStorage != NULL )
    {
        if (  pVarStorage->flags & VARFLAG_TRIGGER )
        {
            /* send out notifications */
            result = varlist_SendNotifications( clientPID,
                                                pVarStorage,
                                                hVar );
        }
    }

    return result;
}

/*============================================================================*/
/*  varlist_SetDirty                                                          */
/*!
    Set the variable's dirty bit

    The varlist_SetDirty function sets the dirty bit for the variable
    if it is a non-volatile variable.  The dirty bit is used to identify
    variables that have been modified after startup.

    @param[in]
        pVarStorage
            Pointer to the variable storage

==============================================================================*/
static void varlist_SetDirty( VarStorage *pVarStorage )
{
    if ( pVarStorage != NULL )
    {
        if (  pVarStorage->flags & VARFLAG_VOLATILE )
        {
            /* do nothing for volatile flags */
        }
        else
        {
            /* set the dirty bit for non-volatile variables */
            pVarStorage->flags |= VARFLAG_DIRTY;
        }
    }
}

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
    VarID *pVarID;
    VarStorage *pVarStorage = NULL;

    if( ( pVarClient != NULL ) &&
        ( pVarInfo != NULL ) )
    {
        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            pVarStorage = pVarID->pVarStorage;
        }

        if ( pVarStorage != NULL )
        {
            /* get the storage reference identifier */
            pVarInfo->storageRef = pVarStorage->storageRef;

            /* copy the format specifier */
            memcpy( pVarClient->variableInfo.formatspec,
                    pVarStorage->formatspec,
                    MAX_FORMATSPEC_LEN );

            /* copy the variable type from the varstore */
            varType = pVarStorage->var.type;
            pVarClient->variableInfo.var.type = varType;

            /* copy the variable length from the varstore */
            len = pVarStorage->var.len;
            pVarClient->variableInfo.var.len = len;

            if( varType == VARTYPE_STR )
            {
                result = varlist_CopyVarInfoStringToClient( pVarClient,
                                                            pVarInfo );
            }
            else if ( varType == VARTYPE_BLOB )
            {
                result = varlist_CopyVarInfoBlobToClient( pVarClient,
                                                          pVarInfo );
            }
            else
            {
                /* copy the source VarObject to the VarClient */
                pVarClient->variableInfo.var.val = pVarInfo->var.val;

                result = EOK;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  varlist_CalcError                                                         */
/*!
    Function to be applied while unblocking CALC blocked clients

    The varlist_CalcError function is applied during unblocking of CALC blocked
    clients when an error has occurred during the CALC processing.
    It updates the clients variable object from the stale data in the
    varstorage.

    @param[in,out]
        pVarClient
            Pointer to the VarClient to receive the error

    @param[in]
        arg
            opaque pointer to the source client

    @retval EOK the error was successfully delivered
    @retval EINVAL invalid arguments

==============================================================================*/
static int varlist_CalcError( VarClient *pVarClient, void *arg )
{
    int result = EINVAL;
    VarClient *pRespondingClient = (VarClient *)arg;
    VarType varType;
    size_t len;
    VarID *pVarID;
    VarStorage *pVarStorage = NULL;
    VarInfo *pVarInfo;

    if( ( pVarClient != NULL ) &&
        ( pRespondingClient != NULL ) )
    {
        pVarClient->responseVal = pRespondingClient->responseVal;

        pVarInfo = &pRespondingClient->variableInfo;

        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            pVarStorage = pVarID->pVarStorage;
        }

        if ( pVarStorage != NULL )
        {
            /* get the storage reference identifier */
            pVarInfo->storageRef = pVarStorage->storageRef;

            /* copy the format specifier */
            memcpy( pVarClient->variableInfo.formatspec,
                    pVarStorage->formatspec,
                    MAX_FORMATSPEC_LEN );

            /* copy the variable type from the varstore */
            varType = pVarStorage->var.type;
            pVarClient->variableInfo.var.type = varType;

            /* copy the variable length from the varstore */
            len = pVarStorage->var.len;
            pVarClient->variableInfo.var.len = len;

            if( varType == VARTYPE_STR )
            {
                /* copy old string from varstorage */
                result = varlist_CopyVarStorageStringToClient( pVarClient,
                                                               pVarStorage );
            }
            else if ( varType == VARTYPE_BLOB )
            {
                /* copy old blob from varstorage */
                result = varlist_CopyVarStorageBlobToClient( pVarClient,
                                                             pVarStorage );
            }
            else
            {
                /* copy the source VarObject to the VarClient */
                pVarClient->variableInfo.var.val = pVarStorage->var.val;

                result = EOK;
            }
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
/*  varlist_CopyVarStorageBlobToClient                                        */
/*!
    Copy a VarStorage blob into the client's working buffer

    The varlist_CopyVarStorageBlobToClient function copies the VarStorage
    object var pointed to by the pVarStorage argument into the working buffer
    of the client pointed to by pVarClient

    Blobs are copied directly into the client's working buffer

    @param[in,out]
        pVarClient
            Pointer to the VarClient containing the working buffer to receive
            the blob data

    @param[in]
        pVarStorage
            pointer to the VarStorage object containing the blob to copy

    @retval EOK the blob was successfully copied
    @retval EINVAL invalid arguments
    @retval ENOENT no blob data contained in the source VarStorage object
    @retval E2BIG the blob in the source VarObject will not fit in the
            client's working buffer

==============================================================================*/
static int varlist_CopyVarStorageBlobToClient( VarClient *pVarClient,
                                               VarStorage *pVarStorage )
{
    int result = EINVAL;
    size_t destlen;

    if ( ( pVarClient != NULL ) &&
         ( pVarStorage != NULL ) )
    {
        /* get the destination blob size */
        destlen = pVarClient->variableInfo.var.len;

        /* check that the blob will fit */
        if( ( pVarStorage->var.len <= destlen ) &&
            ( destlen <= pVarClient->workbufsize ) )
        {
            if( pVarStorage->var.val.blob != NULL )
            {
                /* copy the blob value into the client's working buffer */
                memcpy( &pVarClient->workbuf,
                        pVarStorage->var.val.blob,
                        pVarStorage->var.len );

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
/*  varlist_CopyVarStorageStringToClient                                      */
/*!
    Copy a Var Storage string into the client's working buffer

    The varlist_CopyVarStorageStringToClient function copies the VarStorage
    var object data into the working buffer of the client pointed to
    by pVarClient

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
    @retval ENOENT no string data contained in the source VarStorage object
    @retval E2BIG the string in the source VarObject will not fit in the
            client's working buffer

==============================================================================*/
static int varlist_CopyVarStorageStringToClient( VarClient *pVarClient,
                                                 VarStorage *pVarStorage )
{
    int result = EINVAL;
    size_t destlen;

    if ( ( pVarClient != NULL ) &&
         ( pVarStorage != NULL ) )
    {
        /* get the destination blob size */
        destlen = pVarClient->variableInfo.var.len;

        /* check that the blob will fit */
        if( ( pVarStorage->var.len <= destlen ) &&
            ( destlen <= pVarClient->workbufsize ) )
        {
            if( pVarStorage->var.val.str != NULL )
            {
                /* copy the variable value into the client's working buffer */
                strcpy( &pVarClient->workbuf, pVarStorage->var.val.str );

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
                break;

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
                break;

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
                    if ( ll == (int64_t)(pVarStorage->var.val.ull) )
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
            if( n <= pVarStorage->var.len )
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
            if( n <= pVarStorage->var.len )
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
    VarStorage *pVarStorage = NULL;
    VarID *pVarID;

    if( pVarInfo != NULL )
    {
        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            /* get a pointer to the variable storage for this variable */
            pVarStorage = pVarID->pVarStorage;
        }

        if( ( pVarStorage != NULL ) &&
            ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
        {
            /* retrieve the type */
            pVarInfo->var.type = pVarStorage->var.type;

            /* get the storage reference identifier */
            pVarInfo->storageRef = pVarStorage->storageRef;

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
    VarID *pVarID;

    if( pVarInfo != NULL )
    {
        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            if( ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
            {
                /* retrieve the name */
                memcpy( pVarInfo->name, pVarID->name, MAX_NAME_LEN+1 );
                result = EOK;
            }
            else
            {
                result = ENOENT;
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
    VarStorage *pVarStorage = NULL;
    VarID *pVarID;

    if( pVarInfo != NULL )
    {
        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            /* get a pointer to the variable storage for this variable */
            pVarStorage = pVarID->pVarStorage;
        }

        if( ( pVarStorage != NULL ) &&
            ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
        {
            /* retrieve the type */
            pVarInfo->var.len = pVarStorage->var.len;

            /* get the storage reference identifier */
            pVarInfo->storageRef = pVarStorage->storageRef;

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
/*  VARLIST_GetFlags                                                          */
/*!
    Handle a FLAGS request from a client

    The VARLIST_GetFlags function handles a FLAGS request from a client.
    It retrieves the flags for the specified variable

    @param[in,out]
        pVarInfo
            Pointer to the variable definition containing the handle
            of the variable to query

    @retval EOK the variable was successfully set
    @retval ENOENT the variable does not exist
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_GetFlags( VarInfo *pVarInfo )
{
    int result = EINVAL;
    VarStorage *pVarStorage = NULL;
    VarID *pVarID;

    if( pVarInfo != NULL )
    {
        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            /* get a pointer to the variable storage for this variable */
            pVarStorage = pVarID->pVarStorage;
        }

        if( ( pVarStorage != NULL ) &&
            ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
        {
            /* retrieve the flags */
            pVarInfo->flags = pVarStorage->flags;

            /* get the storage reference identifier */
            pVarInfo->storageRef = pVarStorage->storageRef;

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
/*  VARLIST_GetInfo                                                           */
/*!
    Handle a variable information request from a client

    The VARLIST_GetInfo function handles an INFO request from a client.
    It retrieves the VarInfo data for the specified variable

    @param[in,out]
        pVarInfo
            Pointer to the variable definition containing the handle
            of the variable to query

    @retval EOK the variable was successfully set
    @retval ENOENT the variable does not exist
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_GetInfo( VarInfo *pVarInfo )
{
    int result = EINVAL;
    VarStorage *pVarStorage = NULL;
    VarID *pVarID;

    if( pVarInfo != NULL )
    {
        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            /* get a pointer to the variable storage for this variable */
            pVarStorage = pVarID->pVarStorage;
        }

        if( ( pVarStorage != NULL ) &&
            ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
        {
            /* retrieve the VarInfo object */
            pVarInfo->storageRef = pVarStorage->storageRef;
            pVarInfo->flags = pVarStorage->flags;
            memcpy( pVarInfo->formatspec,
                    pVarStorage->formatspec,
                    MAX_FORMATSPEC_LEN );

            pVarInfo->guid = pVarID->guid;
            pVarInfo->instanceID = pVarID->instanceID;
            memcpy( pVarInfo->name, pVarID->name, MAX_NAME_LEN );
            pVarInfo->permissions = pVarStorage->permissions;
            TAGLIST_TagsToString( pVarStorage->tags,
                                  MAX_TAGS_LEN,
                                  pVarInfo->tagspec,
                                  MAX_TAGSPEC_LEN );
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
/*  VARLIST_GetAliases                                                        */
/*!
    Get a list of aliases for the specified variable

    The VARLIST_GetAliases function genreates a list of aliases
    for the specified variable as a JSON list
    eg. ["alias1","alias2",...,"aliasN"]

    @param[in]
        pVarInfo
            Pointer to the variable definition containing the handle
            of the variable to query

    @param[in,out]
        aliases
            pointer to the buffer to store the alias list

    @param[in]
        len
            maximum length of the alias list

    @retval EOK the alias list was successfully generated
    @retval E2BIG the alias list does not fit in the buffer
    @retval ENOENT there are no aliases
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_GetAliases( VarInfo *pVarInfo, VAR_HANDLE *aliases, size_t len )
{
    int result = EINVAL;
    VarStorage *pVarStorage = NULL;
    VarID *pVarID;
    VarAlias *pVarAlias;
    size_t count = 0;

    if ( ( pVarInfo != NULL ) &&
         ( aliases != NULL ) &&
         ( len > 0 ) )
    {
        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            /* get a pointer to the variable storage for this variable */
            pVarStorage = pVarID->pVarStorage;
        }

        if( ( pVarStorage != NULL ) &&
            ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
        {
            /* pointer to the list of aliases */
            pVarAlias = pVarStorage->pAliases;

            /* copy the aliases */
            while( ( pVarAlias != NULL ) && ( count < len ) )
            {
                /* get a pointer to the variable id */
                pVarID = pVarAlias->pVarID;

                if ( ( pVarID != NULL ) &&
                     ( pVarID->hVar != VAR_INVALID ) )
                {
                    /* store the alias */
                    aliases[count++] = pVarID->hVar;
                }

                /* get the next alias */
                pVarAlias = pVarAlias->pNext;
            }

            if ( count == len )
            {
                result = E2BIG;
            }
            else
            {
                result = ( count > 0 ) ? EOK : ENOENT;
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
    VarStorage *pVarStorage = NULL;
    NotificationType notifyType;
    VarID *pVarID;
    VAR_HANDLE hVar = VAR_INVALID;
    uint32_t flags;

    if( pVarInfo != NULL )
    {
        /* get modification flags */
        flags = pVarInfo->flags;

        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            /* get a pointer to the variable storage for this variable */
            pVarStorage = pVarID->pVarStorage;

            /* get the handle of the variable for the notification request */
            hVar = pVarInfo->hVar;
        }

        /* get the notification type */
        notifyType = pVarInfo->notificationType;

        if( ( pVarStorage != NULL ) &&
            ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
        {
            /* get the storage reference identifier */
            pVarInfo->storageRef = pVarStorage->storageRef;

            /* add the notification */
            result = NOTIFY_Add( &pVarStorage->pNotifications,
                                 notifyType,
                                 hVar,
                                 pid,
                                 flags );
            if( result == EOK )
            {
                if( notifyType == NOTIFY_MODIFIED )
                {
                    pVarStorage->notifyMask |= NOTIFY_MASK_MODIFIED;
                }
                else if ( notifyType == NOTIFY_MODIFIED_QUEUE )
                {
                    pVarStorage->notifyMask |= NOTIFY_MASK_MODIFIED_QUEUE;
                }
                else if( notifyType == NOTIFY_CALC )
                {
                    pVarStorage->notifyMask |= NOTIFY_MASK_CALC;
                }
                else if( notifyType == NOTIFY_VALIDATE )
                {
                    pVarStorage->notifyMask |= NOTIFY_MASK_VALIDATE;
                }
                else if( notifyType == NOTIFY_PRINT )
                {
                    pVarStorage->notifyMask |= NOTIFY_MASK_PRINT;
                }
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  VARLIST_NotifyCancel                                                     */
/*!
    Handle a notification cancellation request from a client

    The VARLIST_NotifyCancel function handles a notification cancellation
    request from a client.

    @param[in]
        pVarInfo
            Pointer to the variable definition containing the handle
            of the variable to cancel the nofication for.

    @param[in]
        pid
            process ID of the requester

    @retval EOK the notification request was successfully registered
    @retval ENOENT the variable does not exist
    @retval ENOTSUP the notification type is not supported
    @retval EINVAL invalid arguments

==============================================================================*/
int VARLIST_NotifyCancel( VarInfo *pVarInfo, pid_t pid )
{
    int result = EINVAL;
    VarStorage *pVarStorage = NULL;
    NotificationType notifyType;
    VarID *pVarID;
    VAR_HANDLE hVar = VAR_INVALID;
    int count = -1;

    if ( pVarInfo != NULL )
    {
        pVarID = varlist_GetVarID( pVarInfo );
        if ( pVarID != NULL )
        {
            /* get a pointer to the variable storage for this variable */
            pVarStorage = pVarID->pVarStorage;

            /* get the handle of the variable for the notification request */
            hVar = pVarInfo->hVar;
        }

        /* get the notification type */
        notifyType = pVarInfo->notificationType;

        if( ( pVarStorage != NULL ) &&
            ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
        {
            /* cancel the specific notification for the specified client */
            result = NOTIFY_Cancel( &pVarStorage->pNotifications,
                                    notifyType,
                                    hVar,
                                    pid,
                                    &count );
            if ( ( result == EOK ) &&
                 ( count == 0 ) )
            {
                /* update the notification masks */
                if( notifyType == NOTIFY_MODIFIED )
                {
                    pVarStorage->notifyMask &= ~NOTIFY_MASK_MODIFIED;
                }
                else if ( notifyType == NOTIFY_MODIFIED_QUEUE )
                {
                    pVarStorage->notifyMask &= ~NOTIFY_MASK_MODIFIED_QUEUE;
                }
                else if( notifyType == NOTIFY_CALC )
                {
                    pVarStorage->notifyMask &= ~NOTIFY_MASK_CALC;
                }
                else if( notifyType == NOTIFY_VALIDATE )
                {
                    pVarStorage->notifyMask &= ~NOTIFY_MASK_VALIDATE;
                }
                else if( notifyType == NOTIFY_PRINT )
                {
                    pVarStorage->notifyMask &= ~NOTIFY_MASK_PRINT;
                }
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
        pVarID
            Pointer to the variable identifier

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
static int AssignVarInfo( VarID *pVarID,
                          VarStorage *pVarStorage,
                          VarInfo *pVarInfo )
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
            pVarID->instanceID = pVarInfo->instanceID;

            /* copy the variable name */
            strncpy(pVarID->name, pVarInfo->name, MAX_NAME_LEN );
            pVarID->name[MAX_NAME_LEN-1] = 0;

            /* set the variable's GUID */
            pVarID->guid = pVarInfo->guid;

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
            pVarStorage->formatspec[MAX_FORMATSPEC_LEN-1] = 0;

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
    SearchContext *ctx;
    VarStorage *pVarStorage = NULL;
    VarID *pVarID;

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
                   ( hVar <= (VAR_HANDLE)varcount ) )
            {
                pVarID = &varstore[hVar];
                if ( pVarID != NULL )
                {
                    /* get a pointer to the storage for this variable */
                    pVarStorage = pVarID->pVarStorage;
                }

                if ( ( pVarStorage != NULL ) &&
                     ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
                {
                    if( varlist_Match( pVarID, ctx ) == EOK )
                    {
                        /* copy the name */
                        memcpy( pVarInfo->name,
                                pVarID->name,
                                MAX_NAME_LEN+1 );

                        /* copy the instanceID */
                        pVarInfo->instanceID = pVarID->instanceID;

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
            *context = 0;
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
    SearchContext *ctx;
    VarStorage *pVarStorage = NULL;
    VarID *pVarID;

    /* get the search context */
    ctx = varlist_FindSearchContext( clientPID, context );
    if ( ctx != NULL )
    {
        result = ENOENT;

        ctx->hVar++;
        hVar = ctx->hVar;

        /* search through the variable list looking for a match */
        while( ( hVar < VARSERVER_MAX_VARIABLES ) &&
               ( hVar <= (VAR_HANDLE)varcount ) )
        {
            pVarID = &varstore[hVar];
            if ( pVarID != NULL )
            {
                /* get a pointer to the storage for this variable */
                pVarStorage = pVarID->pVarStorage;
            }

            if ( ( pVarStorage != NULL ) &&
                 ( varlist_CheckReadPermissions( pVarInfo, pVarID ) ) )
            {
                /* check if the current variable matches the search criteria */
                if( varlist_Match( pVarID, ctx ) == EOK )
                {
                    /* copy the name */
                    memcpy( pVarInfo->name, pVarID->name, MAX_NAME_LEN+1 );

                    /* copy the instanceID */
                    pVarInfo->instanceID = pVarID->instanceID;

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
        *response = 0;

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
        p->query.instanceID = pVarInfo->instanceID;
        p->query.flags = pVarInfo->flags;
        p->query.type = searchType;
        memcpy(&(p->query.tagspec), &(pVarInfo->tagspec), MAX_TAGSPEC_LEN );
        p->query.match = strdup( searchText );
        TAGLIST_Parse( pVarInfo->tagspec,
                       p->tags,
                       MAX_TAGS_LEN );
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
        pVarID
            pointer to the variable to match

    @param[in]
        ctx
            search context containing the search parameters

    @retval EOK the variable was successfully matched against the search context
    @retval EINVAL invalid arguments
    @retval ENOENT the variable did not match the search context

==============================================================================*/
static int varlist_Match( VarID *pVarID, SearchContext *ctx )
{
    int result = EINVAL;
    VarStorage *pVarStorage = NULL;
    int searchtype;
    bool found = false;
    bool match;

    if ( ( ctx != NULL ) &&
         ( pVarID != NULL ) )
    {
        /* get a pointer to the variable storage for this variable */
        pVarStorage = pVarID->pVarStorage;
        if ( pVarStorage != NULL )
        {
            found = true;

            searchtype = ctx->query.type;

            /* dont find hidden variables in vars queries */
            if( pVarStorage->flags & VARFLAG_HIDDEN )
            {
                found = false;
            }

            /* name matching */
            if( searchtype & QUERY_MATCH )
            {
                found &= (strcasestr(pVarID->name, ctx->query.match) != NULL );
            }

            /* regex matching */
            if( searchtype & QUERY_REGEX )
            {
                regex_t regex;
                regmatch_t match[1];

                if( regcomp(&regex, ctx->query.match, REG_EXTENDED) == 0 )
                {
                    found &= (regexec(&regex, pVarID->name, 1, match, 0) == 0 );
                    regfree(&regex);
                }
            }

            /* instance ID matching */
            if( searchtype & QUERY_INSTANCEID )
            {
                found &= (ctx->query.instanceID == pVarID->instanceID );
            }

            /* any flags matching */
            if( searchtype & QUERY_FLAGS )
            {
                match = (ctx->query.flags & pVarStorage->flags ) ? true : false;

                if (searchtype & QUERY_NEGATE_FLAGS)
                {
                    match = !match;
                }

                found &= match;
            }

            /* all tags matching */
            if ( searchtype & QUERY_TAGS )
            {
                if ( varlist_MatchTags( pVarStorage->tags, ctx->tags ) == EOK )
                {
                    match = true;
                }
                else
                {
                    match = false;
                }

                found &= match;
            }
        }

        result = ( found == true ) ? EOK : ENOENT;
    }

    return result;
}

/*============================================================================*/
/*  varlist_MatchTags                                                         */
/*!
    Match all tags

    The varlist_MatchTags function checks if all of the tags in the
    needle list are found in the haystack list.

    @param[in]
        pHaystack
            pointer to the list of tags to search

    @param[in]
        pNeedle
            pointer to the list of tags to find

    @retval EOK all the tags in the needle list were found in the haystack list
    @retval EINVAL invalid arguments
    @retval ENOENT one or more of the tags could not be found

==============================================================================*/
static int varlist_MatchTags( uint16_t *pHaystack, uint16_t *pNeedle )
{
    int result = EINVAL;
    int i;
    int j;
    bool found;

    if ( ( pHaystack != NULL ) &&
         ( pNeedle != NULL ) )
    {
        /* assume we have found all needles until we have not */
        result = EOK;

        /* we must find ALL the needles in the haystack or the search fails */
        for( i = 0; i < MAX_TAGS_LEN ; i++ )
        {
            if ( pNeedle[i] == 0 )
            {
                break;
            }

            found = false;

            /* search the haystack for the needle */
            for ( j = 0; j < MAX_TAGS_LEN; j++ )
            {
                if ( pHaystack[j] == 0 )
                {
                    break;
                }

                if ( pHaystack[j] == pNeedle[i] )
                {
                    found = true;
                }
            }

            /* if the needle is not found, the match fails */
            if ( found == false )
            {
                result = ENOENT;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  VARLIST_GetObj                                                            */
/*!
    Get a pointer to the VarObject specified by the VAR_HANDLE

    The VARLIST_GetObj function gets a pointer to the VarObject
    associated with the specified VAR_HANDLE

    @param[in]
        hVarq
            handle to the variable to get

    @retval pointer to the VarObject associated with the VAR_HANDLE

==============================================================================*/
VarObject *VARLIST_GetObj( VAR_HANDLE hVar )
{
    VarID *pVarID;
    VarStorage *pVarStorage = NULL;
    VarInfo info;
    VarObject *pVarObject = NULL;

    /* set up an info object */
    info.hVar = hVar;

    pVarID = varlist_GetVarID( &info );
    if ( pVarID != NULL )
    {
        /* get a pointer to the variable storage for this variable */
        pVarStorage = pVarID->pVarStorage;
        if ( pVarStorage != NULL )
        {
            pVarObject = &pVarStorage->var;
        }
    }

    return pVarObject;
}

/*============================================================================*/
/*  varlist_GetNotificationPayload                                            */
/*!
    Get a notification payload ready to send to clients

    The varlist_GetNotificationPayload function populates a notification
    payload ready to send to clients which have requested a notification
    on change for the specified variable

    @param[in]
        hVar
            handle to the variable to get the payload for

    @param[in]
        pVarStorage
            pointer to the variable storage containing the payload data

    @param[in,out]
        size
            pointer to a location to store the payload size

    @retval pointer to the prepared payload to send
    @retval NULL if the payload could not be sent

==============================================================================*/
static void *varlist_GetNotificationPayload( VAR_HANDLE hVar,
                                             VarStorage *pVarStorage,
                                             size_t *size )
{
    static char buf[VARSERVER_MAX_NOTIFICATION_MSG_SIZE];
    VarNotification *pVarNotification;
    void *p = &buf[sizeof(VarNotification)];
    size_t len = VARSERVER_MAX_NOTIFICATION_MSG_SIZE - sizeof(VarNotification);
    size_t n;

    pVarNotification = (VarNotification *)buf;

    pVarNotification->hVar = hVar;
    pVarNotification->obj.type = pVarStorage->var.type;
    pVarNotification->obj.len = pVarStorage->var.len;
    pVarNotification->obj.val = pVarStorage->var.val;

    if ( pVarStorage->var.type == VARTYPE_BLOB )
    {
        /* check that the blob fits in the notification */
        if ( ( pVarStorage->var.len <= len ) &&
             ( pVarStorage->var.val.blob != NULL ) )
        {
            /* copy the blob to the notification */
            memcpy( p, pVarStorage->var.val.blob, pVarStorage->var.len );
            *size = sizeof(VarNotification) + pVarStorage->var.len;
        }
        else
        {
            /* too big */
            *size = 0;
        }
    }
    else if ( pVarStorage->var.type == VARTYPE_STR )
    {
        if ( pVarStorage->var.val.str != NULL )
        {
            /* check that the string value fits in the notification */
            n = strlen( pVarStorage->var.val.str );
            if ( n < len )
            {
                /* copy the string to the notification */
                strcpy( p, pVarStorage->var.val.str );
            }
            else
            {
                /* too big */
                *size = 0;
            }
        }
        else
        {
            /* source string is invalid */
            *size = 0;
        }
    }
    else
    {
        *size = sizeof( VarNotification );
    }

    return *size ? buf : NULL;
}

/*============================================================================*/
/*  varlist_CheckReadPermissions                                              */
/*!
    Check a client's read permissions on a variable

    The varlist_CheckReadPermissions function checks a client's
    read permissions against the read permissions for the variable
    to see if the client has access to the variable.

    @param[in]
        pVarInfo
            pointer to the VarInfo request containing the client's
            read permissions

    @param[in]
        pVarID
            pointer to the variable ID containing the variable
            read permissions

    @retval true the client has permission to read
    @retval false the client does not have permission to read

==============================================================================*/
static bool varlist_CheckReadPermissions( VarInfo *pVarInfo,
                                          VarID *pVarID )
{
    register size_t n;
    register size_t m;
    register size_t i;
    register size_t j;
    register bool access = false;
    register gid_t *p;
    register gid_t *q;
    VarStorage *pVarStorage = NULL;

    if ( pVarID != NULL )
    {
        pVarStorage = pVarID->pVarStorage;
    }

    if ( ( pVarInfo != NULL ) &&
         ( pVarStorage != NULL ) )
    {
        n = pVarInfo->ncreds;
        m = pVarStorage->permissions.nreads;
        p = pVarInfo->creds;
        q = pVarStorage->permissions.read;

        for(i = 0 ; i < n && !access ; i++ )
        {
            if ( ( p[i] == 0 ) || ( p[i] == varserver_uid ) )
            {
                access = true;
            }
            else
            {
                for( j = 0; j < m && !access ; j++ )
                {
                    if ( p[i] == q[j])
                    {
                        access = true;
                    }
                }
            }
        }
    }

    return access;
}

/*============================================================================*/
/*  varlist_CheckWritePermissions                                             */
/*!
    Check a client's write permissions on a variable

    The varlist_CheckWritePermissions function checks a client's
    write permissions against the write permissions for the variable
    to see if the client has write access to the variable.

    @param[in]
        pVarInfo
            pointer to the VarInfo request containing the client's
            read/write permissions

    @param[in]
        pVarID
            pointer to the variable ID containing the variable
            write permissions

    @retval true the client has permission to write
    @retval false the client does not have permission to write

==============================================================================*/
static bool varlist_CheckWritePermissions( VarInfo *pVarInfo,
                                           VarID *pVarID )
{
    register size_t n;
    register size_t m;
    register size_t i;
    register size_t j;
    register bool access = false;
    register gid_t *p;
    register gid_t *q;
    VarStorage *pVarStorage;

    if ( ( pVarInfo != NULL ) &&
         ( pVarID != NULL ) )
    {
        pVarStorage = pVarID->pVarStorage;
        if ( pVarStorage != NULL )
        {
            /* client group IDs are always in the varInfo read list
            even when checking for write permissions */
            n = pVarInfo->ncreds;
            m = pVarStorage->permissions.nwrites;
            p = pVarInfo->creds;
            q = pVarStorage->permissions.write;

            for(i = 0 ; i < n && !access ; i++ )
            {
                if ( ( p[i] == 0 ) || ( p[i] == varserver_uid ) )
                {
                    access = true;
                }
                else
                {
                    for( j = 0; j < m && !access ; j++ )
                    {
                        if ( p[i] == q[j])
                        {
                            access = true;
                        }
                    }
                }
            }

        }
    }

    return access;
}

/*============================================================================*/
/*  varlist_HandleMetric                                                      */
/*!
    Handle writes to metric type variables

    The varlist_HandleMetric function handles a write to a variable that
    has the "metric" flag set.  If the value to be written is zero, the
    metric is cleared to zero.  If the value to be written is non-zero,
    the metric is incremented.  Only unsigned integer type variables support
    metric operations, all others will return ENOTSUP

    @param[in]
        pVarInfo
            pointer to the VarInfo request

    @param[in]
        pVarStorage
            pointer to the variable storage information

    @retval EOK Ok to proceed
    @retval EINVAL invalid arguments
    @retval ENOTSUP metric operations not supported on this variable

==============================================================================*/
static int varlist_HandleMetric( VarInfo *pVarInfo,
                                 VarStorage *pVarStorage )
{
    int result = EINVAL;

    if ( ( pVarInfo != NULL ) && ( pVarStorage != NULL ) )
    {
        result = EOK;

        if ( pVarStorage->flags & VARFLAG_METRIC )
        {
            switch( pVarStorage->var.type )
            {
                case VARTYPE_UINT16:
                    if ( pVarInfo->var.val.ui != 0 )
                    {
                        pVarInfo->var.val.ui = pVarStorage->var.val.ui + 1;
                    }
                    break;

                case VARTYPE_UINT32:
                    if ( pVarInfo->var.val.ul != 0 )
                    {
                        pVarInfo->var.val.ul = pVarStorage->var.val.ul + 1;
                    }
                    break;

                case VARTYPE_UINT64:
                    if ( pVarInfo->var.val.ull != 0 )
                    {
                        pVarInfo->var.val.ull = pVarStorage->var.val.ull + 1;
                    }
                    break;

                default:
                    break;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  VARLIST_SetUser                                                           */
/*!
    Set the varserver user identifier

    The VARLIST_SetUser function sets the identifier of the user that
    started the variable server.  This user identifier is used to validate
    request to read/write variables. If the requesting user is the same
    as the user that started the variable server, then this requesting user
    has full read/write access to the variable store.

==============================================================================*/
void VARLIST_SetUser( void )
{
    /* get the user identifier of the user that started the variable server */
    varserver_uid = getuid();
}

/*============================================================================*/
/*  varlist_GetVarID                                                          */
/*!
    Get a pointer to the VarID object

    The varlist_GetVarID function gets a pointer to the VarID object
    given the pointer to the VarInfo object.

    @param[in]
        pVarInfo
            pointer to the VarInfo request containing the client's
            read/write permissions

    @retval pointer to the VarID object
    @retval NULL if the VarID object cannot be retrieved

==============================================================================*/
static VarID *varlist_GetVarID( VarInfo *pVarInfo )
{
    VAR_HANDLE hVar;
    VarID *pVarID = NULL;

    if ( pVarInfo != NULL )
    {
        hVar = pVarInfo->hVar;

        if( ( hVar < VARSERVER_MAX_VARIABLES ) &&
            ( hVar <= (VAR_HANDLE)varcount ) )
        {
            pVarID = &varstore[hVar];
        }
    }

    return pVarID;
}


/*! @}
 * end of varlist group */
