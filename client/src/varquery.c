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
 * @defgroup varquery varquery
 * @brief Handle searches of the variable server
 * @{
 */

/*==========================================================================*/
/*!
@file varquery.c

    Variable Search against the Variable Server

    The Variable Query provides a mechanism to search
    for variables registered with the Variable Server using various
    search criteria since as name matching, flags matching, tags matching,
    and instance ID matching.

*/
/*==========================================================================*/


/*============================================================================
        Includes
============================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "var.h"
#include "varserver.h"
#include "varquery.h"

/*============================================================================
        Private definitions
============================================================================*/

/*============================================================================
        Type Definitions
============================================================================*/

/*============================================================================
        Private function declarations
============================================================================*/

/*============================================================================
        Function definitions
============================================================================*/

/*==========================================================================*/
/*  VARQUERY_Search                                                         */
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
        instanceID
            used for instance ID matching if QUERY_INSTANCEID is specified,
            otherwise it is ignored.

    @param[in]
        fd
            output steam for variable data

    @retval EOK - variable search was successful
    @retval EINVAL - invalid arguments
    @retval ENOENT - no variables matched the search criteria

============================================================================*/
int VARQUERY_Search( VARSERVER_HANDLE hVarServer,
                     int searchType,
                     char *match,
                     uint32_t instanceID,
                     VarFlags flags,
                     int fd )
{
    int result = EINVAL;
    VarQuery query;
    VarObject obj;

    memset( &query, 0, sizeof( VarQuery ) );

    query.type = searchType;
    query.instanceID = instanceID;
    query.match = match;
    query.flags = flags;

    result = VAR_GetFirst( hVarServer, &query, &obj );
    while ( result == EOK )
    {
        dprintf(fd, "%s", query.name );

        if( searchType & QUERY_SHOWVALUE )
        {
            dprintf(fd, "=" );
            VAR_Print( hVarServer, query.hVar, fd );
        }

        dprintf(fd, "\n");

        result = VAR_GetNext( hVarServer, &query, &obj );
    }

    return result;
}

/*! @}
 * end of varquery group */
