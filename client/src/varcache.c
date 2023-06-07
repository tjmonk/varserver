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

/*!
 * @defgroup varcache varcache
 * @brief Handle a cache of variable handles
 * @{
 */

/*============================================================================*/
/*!
@file varcache.c

    Variable Cache Utility Functions

    The Variable Cache utility functions provide a mechanism to manage
    a cache of variable handles on the HEAP.  The cache can be thought
    of as an unordered set of variable handles.

    Caches can be combined and manipulated with AND/OR style logic.

    Variable handles can be added to or removed from a cache.

    Queries can be made to determine if a variable handle is in a
    cache or not.

    Cache primitives (will eventually) include:

    Add
    Remove
    Exists
    And
    Or

    The VarCache object is an opaque object (i.e the caller has a handle
    to the cache but cannot interrogate its content except via published
    API calls).

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
#include <sys/un.h>
#include <string.h>
#include <errno.h>
#include "varobject.h"
#include "varclient.h"
#include "varserver.h"
#include "var.h"
#include "varcache.h"

/*==============================================================================
        Private type declarations
==============================================================================*/

/*! VarCache object */
typedef struct _VarCache
{
    /*! maximum length of the cache */
    size_t maxLength;

    /*! current length of the cache */
    size_t length;

    /*! size to grow the cache by when it is full */
    size_t growBy;

    /*! pointer to the variable handles in the cache */
    VAR_HANDLE *pVars;

} VarCache;

/*==============================================================================
        Private function declarations
==============================================================================*/

/*==============================================================================
        File scoped variables
==============================================================================*/

/*==============================================================================
        Function definitions
==============================================================================*/

/*============================================================================*/
/*  VARCACHE_Init                                                             */
/*!

    Initialize a new Variable Cache on the HEAP

@param[in,out]
    ppVarCache
        pointer to a location to store the newly created VarCache pointer

@param[in]
    len
        length of the variable cache.  specify 0 to use VARCACHE_DEFAULT_SIZE

@param[in]
    growBy
        size to grow the cache by when it gets full.
        Specify 0 to use VARCACHE_DEFAULT_GROW_BY

@retval EOK the variable cache was successfully created
@retval ENOMEM memory allocation failed
@retval EINVAL invalid arguments

==============================================================================*/
int VARCACHE_Init( VarCache **ppVarCache, size_t len, size_t growBy )
{
    VAR_HANDLE *pVars;
    int result = EINVAL;

    if ( ppVarCache != NULL )
    {
        if( len == 0 )
        {
            len = VARCACHE_DEFAULT_SIZE;
        }

        if( growBy == 0 )
        {
            growBy = VARCACHE_DEFAULT_GROW_BY;
        }

        /* allocate memory for the variable list */
        pVars = (VAR_HANDLE *)calloc(1, len * sizeof( VAR_HANDLE ) );
        if( pVars != NULL )
        {
            /* allocate memory for the VarCache object */
            *ppVarCache = (VarCache *)malloc( sizeof( VarCache ) );
            if( *ppVarCache != NULL )
            {
                (*ppVarCache)->length = 0;
                (*ppVarCache)->maxLength = len;
                (*ppVarCache)->growBy = growBy;
                (*ppVarCache)->pVars = pVars;

                /* indicate success */
                result = EOK;
            }
            else
            {
                /* unable to allocate memory for the VarCache object, so free
                 * the variable handle list */
                free( pVars );
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
/*  VARCACHE_Add                                                              */
/*!

    Add a new variable to the variable cache

@param[in]
    pVarCache
        pointer to the VarCache object to add the variable to

@param[in]
    hVar
        handle of the variable to add

@retval EOK the variable was successfully added to the cache
@retval ENOMEM memory reallocation failed
@retval EINVAL invalid arguments

==============================================================================*/
int VARCACHE_Add( VarCache *pVarCache, VAR_HANDLE hVar )
{
    int result = EINVAL;
    VAR_HANDLE *pVars;
    size_t newSize;
    size_t newLength;

    if ( ( pVarCache != NULL ) &&
         ( hVar != VAR_INVALID ) )
    {
        /* point to the array of variable handles */
        pVars = pVarCache->pVars;

        /* store the variable handle at the end of the variable cache */
        pVars[pVarCache->length++] = hVar;

        /* assume everything is ok until it isn't */
        result = EOK;

        if( pVarCache->length == pVarCache->maxLength )
        {
            /* VarCache is full, we need to resize */
            /* calculate the new size of the variable cache */
            newSize = pVarCache->maxLength + pVarCache->growBy;

            /* calculate the new buffer length */
            newLength = newSize * sizeof( VAR_HANDLE );

            /* reallocate space on the heap */
            pVars = realloc( pVarCache->pVars, newLength );
            if( pVars != NULL )
            {
                /* update the VarCache object with its new size and
                 * pointer to the Vars array */
                pVarCache->maxLength = newSize;
                pVarCache->pVars = pVars;
            }
            else
            {
                /* memory allocation failed */
                result = ENOMEM;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  VARCACHE_AddUnique                                                        */
/*!

    Add a new variable to the variable cache, but only if it is
    not already there

@param[in]
    pVarCache
        pointer to the VarCache object to add the variable to

@param[in]
    hVar
        handle of the variable to add

@retval EOK the variable was added to the cache, or was already there
@retval ENOMEM memory reallocation failed
@retval EINVAL invalid arguments

==============================================================================*/
int VARCACHE_AddUnique( VarCache *pVarCache, VAR_HANDLE hVar )
{
    int result = EINVAL;

    if ( ( pVarCache != NULL ) &&
         ( hVar != VAR_INVALID ) )
    {
        /* check if the variable already exists */
        if( VARCACHE_HasVar( pVarCache, hVar ) == true )
        {
            /* variable already exists */
            result = EOK;
        }
        else
        {
            /* add the variable to the cache since it does not already exist */
            result = VARCACHE_Add( pVarCache, hVar );
        }
    }

    return result;
}

/*============================================================================*/
/*  VARCACHE_HasVar                                                           */
/*!

    Check if the specified variable is in the specified variable cache

@param[in]
    pVarCache
        pointer to the VarCache object to check

@param[in]
    hVar
        handle of the variable to look for

@retval true the variable is in the cache
@retval false the variable is not in the cache

==============================================================================*/
bool VARCACHE_HasVar( VarCache *pVarCache, VAR_HANDLE hVar )
{
    bool result = false;
    size_t idx;
    VAR_HANDLE *pVars;

    if ( ( pVarCache != NULL ) &&
         ( hVar != VAR_INVALID ) )
    {
        pVars = pVarCache->pVars;
        for ( idx = 0; idx < pVarCache->length ; idx++ )
        {
            if( pVars[idx] == hVar )
            {
                result = true;
                break;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  VARCACHE_Size                                                             */
/*!

    Get the size of the variable cache

@param[in]
    pVarCache
        pointer to the VarCache object to check

@return the number of variables in the specified variable cache

==============================================================================*/
size_t VARCACHE_Size( VarCache *pVarCache )
{
    size_t length = 0;

    if( pVarCache != NULL )
    {
        length = pVarCache->length;
    }

    return length;
}

/*============================================================================*/
/*  VARCACHE_Map                                                              */
/*!

    Map a function over the variable cache

    The VARCACHE_Map function applies the caller specified function to
    each variable in the Variable Cache.

@param[in]
    pVarCache
        pointer to the VarCache object to apply the mapping function to

@param[in]
    fn
        pointer to the map function to apply to each variable in the cache

@param[in]
    arg
        optional (void *) argument to pass to the map function for each
        variable in the cache

@retval EOK the map function was successfully applied to every variable
            in the variable cache
@retval EINVAL invalid arguments

==============================================================================*/
int VARCACHE_Map( VarCache *pVarCache,
                  int (*fn)(VAR_HANDLE hVar, void *arg),
                  void *arg )
{
    int result = EINVAL;
    int idx;
    VAR_HANDLE hVar;

    if ( ( pVarCache != NULL ) &&
         ( pVarCache->pVars != NULL ) &&
         ( fn != NULL ) )
    {
        /* assume everything is ok until it is not */
        result = EOK;

        /* iterate through every variable in the cache */
        for ( idx = 0; idx < pVarCache->length ; idx++ )
        {
            /* get the variable handle to apply the map function to */
            hVar = pVarCache->pVars[idx];

            /* apply the map function and track the result */
            result |= fn( hVar, arg );
        }
    }

    return result;
}

/*============================================================================*/
/*  VARCACHE_Get                                                              */
/*!

    Get the nth variable from the variable cache

    The VARCACHE_Get function retrieves the handle of the nth
    variable in the variable cache. The caller should not assume
    that the variable cache is ordered, or that the order will
    remain the same after cache addition or combination with
    another cache.

@param[in]
    pVarCache
        pointer to the VarCache object to apply the mapping function to

@param[in]
    idx
        specify which variable handle to retrieve

@retval handle of the variable at the speicified index
@retval VAR_INVALID if there is no variable at the specified index

==============================================================================*/
VAR_HANDLE VARCACHE_Get( VarCache *pVarCache, size_t idx )
{
    VAR_HANDLE hVar = VAR_INVALID;

    if( ( pVarCache != NULL ) &&
        ( pVarCache->pVars != NULL ) )
    {
        if( idx < pVarCache->length )
        {
            hVar = pVarCache->pVars[idx];
        }
    }

    return hVar;
}

/*============================================================================*/
/*  VARCACHE_Clear                                                            */
/*!

    Clear the variable cache

    The VARCACHE_Clear function clears the variable cache data
    and sets the variable cache length to zero.  The currently
    allocated memory on the HEAP remains unchanged.

@param[in]
    pVarCache
        pointer to the VarCache object to clear

@retval EOK the variable cache was cleared
@retval EINVAL invalid arguments

==============================================================================*/
int VARCACHE_Clear( VarCache *pVarCache )
{
    int result = EINVAL;
    size_t length;

    if ( ( pVarCache != NULL ) &&
         ( pVarCache->pVars != NULL ) )
    {
        /* calculate the cache length */
        length = pVarCache->length * sizeof( VAR_HANDLE );

        /* clear the cache */
        memset( pVarCache->pVars, 0, length );

        /* set the cache length to zero */
        pVarCache->length = 0;

        result = EOK;
    }

    return result;
}

/*! @}
 * end of varcache group */
