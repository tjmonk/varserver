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
 * @defgroup varflags varflags
 * @brief Manage variable flags
 * @{
 */

/*============================================================================*/
/*!
@file varflags.c

    Manage Variable Flags

    The VarFlags component provides a mechanism to set or clear variable
    flags by variable name match..

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
#include <varserver/varclient.h>
#include <varserver/varflags.h>

/*==============================================================================
        Private definitions
==============================================================================*/

/*==============================================================================
        Type Definitions
==============================================================================*/

/*==============================================================================
        Private function declarations
==============================================================================*/

/*==============================================================================
        Function definitions
==============================================================================*/

/*============================================================================*/
/*  VAR_ModifyFlags                                                           */
/*!
    Set/clear variable flags for all matching variables

    The VAR_ModifyFlags function searches for variables using the
    specified name and sets or clears the specified flags depending
    on the requested operation.

    @param[in]
        hVarServer
            handle to the Variable Server

    @param[in]
        match
            variable name match string

    @param[in]
        flags
            comma separated list of flags to set/clear

    @param[in]
        operation.  One of: FLAG_SET, FLAG_CLEAR

    @retval EOK - flags update was successful
    @retval EINVAL - invalid arguments
    @retval ENOENT - no variables matched the search criteria

==============================================================================*/
int VAR_ModifyFlags( VARSERVER_HANDLE hVarServer,
                     char *match,
                     uint32_t flags,
                     FlagModifyOperation op )
{
    int result = EINVAL;
    VarQuery query;

    memset( &query, 0, sizeof( VarQuery ) );

    query.type = QUERY_MATCH;
    query.instanceID = 0;
    query.match = match;

    result = VAR_GetFirst( hVarServer, &query, NULL );
    while ( result == EOK )
    {
        if ( op == FlagSet )
        {
            result = VAR_SetFlags( hVarServer, query.hVar, flags );
        }
        else
        {
            result = VAR_ClearFlags( hVarServer, query.hVar, flags );

        }

        result = VAR_GetNext( hVarServer, &query, NULL );
    }

    return result;
}

/*============================================================================*/
/*  VAR_ClearDirtyFlags                                                       */
/*!
    Clear the dirty flag of all dirty variables

    The VAR_ClearDirtyFlags function searches for all variables which have
    their dirty flag set (i.e they have been modified since startup, or
    since the dirty flags were last cleared), and clears the dirty flag
    on those variables.

    @param[in]
        hVarServer
            handle to the Variable Server

    @retval EOK - flags update was successful
    @retval EINVAL - invalid arguments

==============================================================================*/
int VAR_ClearDirtyFlags( VARSERVER_HANDLE hVarServer )
{
    int result = EINVAL;
    VarQuery query;

    memset( &query, 0, sizeof( VarQuery ) );

    query.type = QUERY_FLAGS;

    result = VARSERVER_StrToFlags("dirty", &query.flags );
    if ( result == EOK )
    {
        result = VAR_GetFirst( hVarServer, &query, NULL );
        while ( result == EOK )
        {
            result = VAR_ClearFlags( hVarServer, query.hVar, query.flags );

            result = VAR_GetNext( hVarServer, &query, NULL );
        }
    }

    return result;
}

/*============================================================================*/
/*  VAR_SetFlags                                                              */
/*!
    Set variable flags for the specified variable

    The VAR_SetFlags function sets the specified flags for the specified
    variable.

    @param[in]
        hVarServer
            handle to the Variable Server

    @param[in]
        hVar
            handle to the variable whose flags are to be set

    @param[in]
        flags
            flags bitfield

    @retval EOK - flags update was successful
    @retval EINVAL - invalid arguments
    @retval ENOENT - no variables matched the search criteria

==============================================================================*/
int VAR_SetFlags( VARSERVER_HANDLE hVarServer,
                  VAR_HANDLE hVar,
                  uint32_t flags )
{
    int result = EINVAL;
    VarClient *pVarClient = ValidateHandle( hVarServer );

    if( pVarClient != NULL )
    {
        pVarClient->requestType = VARREQUEST_SET_FLAGS;
        pVarClient->variableInfo.hVar = hVar;
        pVarClient->variableInfo.flags = flags;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
    }

    return result;

}

/*============================================================================*/
/*  VAR_ClearFlags                                                            */
/*!
    Clear variable flags for the specified variable

    The VAR_ClearFlags function clears the specified flags for the
    specified variable.

    @param[in]
        hVarServer
            handle to the Variable Server

    @param[in]
        hVar
            handle to the variable whose flags are to be cleared

    @param[in]
        flags
            flags bitfield

    @param[in]
        operation.  One of: FLAG_SET, FLAG_CLEAR

    @retval EOK - flag clearing was successful
    @retval EINVAL - invalid arguments
    @retval ENOENT - no variables matched the search criteria

==============================================================================*/
int VAR_ClearFlags( VARSERVER_HANDLE hVarServer,
                    VAR_HANDLE hVar,
                    uint32_t flags )
{
    int result = EINVAL;
    VarClient *pVarClient = ValidateHandle( hVarServer );

    if( pVarClient != NULL )
    {
        pVarClient->requestType = VARREQUEST_CLEAR_FLAGS;
        pVarClient->variableInfo.hVar = hVar;
        pVarClient->variableInfo.flags = flags;

        result = ClientRequest( pVarClient, SIG_CLIENT_REQUEST );
    }

    return result;
}

/*! @}
 * end of varflags group */
