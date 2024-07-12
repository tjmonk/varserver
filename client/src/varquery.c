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
 * @defgroup varquery varquery
 * @brief Handle searches of the variable server
 * @{
 */

/*============================================================================*/
/*!
@file varquery.c

    Variable Search against the Variable Server

    The Variable Query provides a mechanism to search
    for variables registered with the Variable Server using various
    search criteria since as name matching, flags matching, tags matching,
    and instance ID matching.

*/
/*============================================================================*/


/*==============================================================================
        Includes
==============================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <varserver/var.h>
#include <varserver/varserver.h>
#include <varserver/varcache.h>
#include <varserver/varquery.h>

/*==============================================================================
        Private definitions
==============================================================================*/

/*==============================================================================
        Type Definitions
==============================================================================*/

/*==============================================================================
        Private function declarations
==============================================================================*/

static int varquery_AddCache( VARSERVER_HANDLE hVarServer,
                              VAR_HANDLE hVar,
                              void *arg );

static int varquery_AddCacheUnique( VARSERVER_HANDLE hVarServer,
                                    VAR_HANDLE hVar,
                                    void *arg );

static int varquery_PrintType( int fd, VarType type );


/*==============================================================================
        Function definitions
==============================================================================*/

/*============================================================================*/
/*  VARQUERY_Search                                                           */
/*!
    Search for variables

    The VARQUERY_Search function searches for variables using the
    specified criteria and outputs them to the specified output.

    @param[in]
        hVarServer
            handle to the Variable Server to create variables for

    @param[in]
        searchType
            a bitfield indicating the type of search to perform.
            Contains one or more of the following OR'd together:
                QUERY_REGEX or QUERY_MATCH
                QUERY_FLAGS
                QUERY_TAGS
                QUERY_INSTANCEID

    @param[in]
        match
            string to use for variable name matching.  This is used
            if one of these search types is specified: QUERY_REGEX,
            QUERY_MATCH, otherwise this parameter is ignored.

    @param[in]
        tagspec
            comma separated list of tags to search for

    @param[in]
        instanceID
            used for instance ID matching if QUERY_INSTANCEID is specified,
            otherwise it is ignored.

    @param[in]
        fd
            output steam for variable data

    @retval EOK - variable search was successful
    @retval EINVAL - invalid arguments
    @retval ENOENT - no variables matched the search criteria

==============================================================================*/
int VARQUERY_Search( VARSERVER_HANDLE hVarServer,
                     int searchType,
                     char *match,
                     char *tagspec,
                     uint32_t instanceID,
                     uint32_t flags,
                     int fd )
{
    int result = EINVAL;
    VarQuery query;
    size_t len;

    memset( &query, 0, sizeof( VarQuery ) );

    query.type = searchType;
    query.instanceID = instanceID;
    query.match = match;
    query.flags = flags;

    if ( tagspec != NULL )
    {
        len = strlen( tagspec );
        if ( len < MAX_TAGSPEC_LEN )
        {
            strcpy( query.tagspec, tagspec );
        }
    }

    result = VAR_GetFirst( hVarServer, &query, NULL );
    while ( result == EOK )
    {
        if ( query.instanceID == 0 )
        {
            dprintf(fd, "%s", query.name );
        }
        else
        {
            dprintf(fd, "[%d]%s", query.instanceID, query.name );
        }

        if ( searchType & QUERY_SHOWTYPE )
        {
            dprintf(fd, "(" );
            varquery_PrintType( fd, query.vartype );
            dprintf(fd, ")" );
        }

        if( searchType & QUERY_SHOWVALUE )
        {
            dprintf(fd, "=" );
            VAR_Print( hVarServer, query.hVar, fd );
        }

        dprintf(fd, "\n");

        result = VAR_GetNext( hVarServer, &query, NULL );
    }

    return result;
}

/*============================================================================*/
/*  VARQUERY_Map                                                              */
/*!
    Perform a variable query and map a function across the variable query result

    The VARQUERY_Map function searches for variables using the
    specified criteria and then maps the specified mapping function
    across the query result to perform an action on all of the
    found variables.

    @param[in]
        hVarServer
            handle to the Variable Server

    @param[in]
        pVarQuery
            pointer to a populated variable query

    @param[in]
        mapfn
            pointer to a map function of the form:
            int mapfn( VARSERVER_HANDLE hVarServer, VAR_HANDLE hVar, void *arg )

    @param[in]
        arg
            opaque void * argument to pass to the map function

    @retval EOK - variable search was successful
    @retval EINVAL - invalid arguments
    @retval ENOENT - no variables matched the search criteria
    @retval other error reported by the map function.

==============================================================================*/
int VARQUERY_Map( VARSERVER_HANDLE hVarServer,
                  VarQuery *pVarQuery,
                  int (*mapfn)( VARSERVER_HANDLE hVarServer,
                                VAR_HANDLE hVar,
                                void *arg ),
                  void *arg )
{
    int result = EINVAL;
    int errcount = 0;
    int count = 0;
    int rc;

    if ( ( pVarQuery != NULL ) &&
         ( mapfn != NULL ) )
    {
        /* get the first variable which matches the search criteria */
        result = VAR_GetFirst( hVarServer, pVarQuery, NULL );
        while ( result == EOK )
        {
            if ( pVarQuery->hVar != VAR_INVALID )
            {
                /* add the found variable to the cache */
                rc = mapfn( hVarServer, pVarQuery->hVar, arg );
                if ( rc == EOK )
                {
                    count++;
                }
                else
                {
                    errcount++;
                }
            }

            /* get the next variable which matches the search criteria */
            result = VAR_GetNext( hVarServer, pVarQuery, NULL );
        }
    }

    if ( errcount == 0 )
    {
        if ( count > 0 )
        {
            result = EOK;
        }
        else
        {
            result = ENOENT;
        }
    }
    else
    {
        /* report the error from VARCACHE_Add */
        result = rc;
    }

    return result;
}

/*============================================================================*/
/*  VARQUERY_Cache                                                            */
/*!
    Populate a VarCache from a variable search

    The VARQUERY_Cache function searches for variables using the
    specified criteria and stores them in the specified variable
    cache.

    @param[in]
        hVarServer
            handle to the Variable Server

    @param[in]
        pVarQuery
            pointer to a populated variable query

    @param[in]
        pVarCache
            pointer to a Variable Cache to populate

    @retval EOK - variable search was successful
    @retval EINVAL - invalid arguments
    @retval ENOENT - no variables matched the search criteria
    @retval other error reported by the map function.

==============================================================================*/
int VARQUERY_Cache( VARSERVER_HANDLE hVarServer,
                    VarQuery *pVarQuery,
                    VarCache *pVarCache )
{
    /* map the varquery_AddCache function over the query result */
    return VARQUERY_Map( hVarServer,
                         pVarQuery,
                         varquery_AddCache,
                         (void *)pVarCache );
}

/*============================================================================*/
/*  VARQUERY_CacheUnique                                                      */
/*!
    Populate a VarCache from a variable search

    The VARQUERY_CacheUnique function searches for variables using the
    specified criteria and stores them in the specified variable
    cache, but only if they are not there already

    @param[in]
        hVarServer
            handle to the Variable Server

    @param[in]
        pVarQuery
            pointer to a populated variable query

    @param[in]
        pVarCache
            pointer to a Variable Cache to populate

    @retval EOK - variable search was successful
    @retval EINVAL - invalid arguments
    @retval ENOENT - no variables matched the search criteria
    @retval other error reported by the map function.

==============================================================================*/
int VARQUERY_CacheUnique( VARSERVER_HANDLE hVarServer,
                          VarQuery *pVarQuery,
                          VarCache *pVarCache )
{
    /* map the varquery_AddCache function over the query result */
    return VARQUERY_Map( hVarServer,
                         pVarQuery,
                         varquery_AddCacheUnique,
                         (void *)pVarCache );
}

/*============================================================================*/
/*  varquery_AddCache                                                         */
/*!
    Mapping callback function to add a variable to a cache

    The varquery_AddCache callback function is used by the
    VARQUERY_Map function to add a variable to a variable cache.

    @param[in]
        hVarServer
            handle to the variable server

    @param[in]
        hVar
            handle of variable to be added to the cache

    @param[in]
        arg
            opaque pointer argument from the map function
            which is converted to a VarCache *

    @retval EOK - cache add was successful
    @retval EINVAL - invalid arguments
    @retval ENOMEM - memory allocation failure

==============================================================================*/
static int varquery_AddCache( VARSERVER_HANDLE hVarServer,
                              VAR_HANDLE hVar,
                              void *arg )
{
    int result = EINVAL;
    VarCache *pVarCache = (VarCache *)arg;

    /* hVarServer is not used */
    (void)hVarServer;

    if ( ( hVar != VAR_INVALID ) &&
         ( pVarCache != NULL ) )
    {
        result = VARCACHE_Add( pVarCache, hVar );
    }

    return result;
}

/*============================================================================*/
/*  varquery_AddCacheUnique                                                   */
/*!
    Mapping callback function to add a variable to a cache

    The varquery_AddCacheUnique callback function is used by the
    VARQUERY_Map function to add a variable to a variable cache,
    but only if it is not there already.

    @param[in]
        hVarServer
            handle to the variable server

    @param[in]
        hVar
            handle of variable to be added to the cache

    @param[in]
        arg
            opaque pointer argument from the map function
            which is converted to a VarCache *

    @retval EOK - cache add was successful
    @retval EINVAL - invalid arguments
    @retval ENOMEM - memory allocation failure

==============================================================================*/
static int varquery_AddCacheUnique( VARSERVER_HANDLE hVarServer,
                                    VAR_HANDLE hVar,
                                    void *arg )
{
    int result = EINVAL;
    VarCache *pVarCache = (VarCache *)arg;

    /* hVarServer is not used */
    (void)hVarServer;

    if ( ( hVar != VAR_INVALID ) &&
         ( pVarCache != NULL ) )
    {
        result = VARCACHE_AddUnique( pVarCache, hVar );
    }

    return result;
}

/*============================================================================*/
/*  varquery_PrintType                                                        */
/*!
    Print the variable type to the specified file descriptor

    The varquery_PrintType function prints the variable type to the
    specified file descriptor.

    @param[in]
        fd
            file descriptor to print the type to

    @param[in]
        type
            variable type to print

    @retval EOK - type print was successful
    @retval ENOTSUP - type is not supported

==============================================================================*/
static int varquery_PrintType( int fd, VarType type )
{
    int result = EINVAL;

    if ( fd >= 0 )
    {
        switch( type )
        {
            case VARTYPE_FLOAT:
                dprintf(fd, "float");
                result = EOK;
                break;

            case VARTYPE_BLOB:
                dprintf(fd, "blob");
                result = EOK;
                break;

            case VARTYPE_STR:
                dprintf(fd, "string");
                result = EOK;
                break;

            case VARTYPE_UINT16:
                dprintf(fd, "uint16");
                result = EOK;
                break;

            case VARTYPE_INT16:
                dprintf(fd, "int16");
                result = EOK;
                break;

            case VARTYPE_UINT32:
                dprintf(fd, "uint32");
                result = EOK;
                break;

            case VARTYPE_INT32:
                dprintf(fd, "int32");
                result = EOK;
                break;

            case VARTYPE_UINT64:
                dprintf(fd, "uint64");
                result = EOK;
                break;

            case VARTYPE_INT64:
                dprintf(fd, "int64");
                result = EOK;
                break;

            default:
                result = ENOTSUP;
                break;
        }
    }

    return result;
}

/*! @}
 * end of varquery group */
