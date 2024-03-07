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
 * @brief Variable Flag Manipulation utility
 * @{
 */

/*============================================================================*/
/*!
@file varflags.c

    Variable Flags Manipulation Utility

    The VarFlags utility can be used to set/clear flags on
    varserver variables which match the specified search criteria.

    Variables can be searched using their name, instance identifier,
    tags, and flags.

    Search options are specified via command line arguments to the varflags
    utility

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <limits.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <varserver/varserver.h>
#include <varserver/varquery.h>
#include <varserver/varflags.h>

/*==============================================================================
       Type Definitions
==============================================================================*/
typedef struct _varFlagsState
{
    /*! handle to the variable server */
    VARSERVER_HANDLE hVarServer;

    /*! search text */
    char *searchText;

    /*! flags to search for */
    uint32_t flags;

    /*! flags to set */
    uint32_t setflags;

    /*! flags to clear */
    uint32_t clearflags;

    /*! type of search - bitfield */
    int searchType;

    /*! instance identifier */
    uint32_t instanceID;

    /*! user name */
    char *username;

    /*! verbose flag */
    bool verbose;

} VarFlagsState;

/*==============================================================================
       Function declarations
==============================================================================*/
static void usage( char *cmdname );
static int ProcessOptions( int argC,
                           char *argV[],
                           VarFlagsState *pState );
static int SetUser( VarFlagsState *pState );
static int varflags_Update( VarFlagsState *pState );

/*==============================================================================
       Definitions
==============================================================================*/

/*==============================================================================
      File Scoped Variables
==============================================================================*/

/*==============================================================================
       Function definitions
==============================================================================*/

/*============================================================================*/
/*  main                                                                      */
/*!
    Main entry point for the vars application

    @param[in]
        argc
            number of arguments on the command line
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @return none

==============================================================================*/
int main(int argC, char *argV[])
{
    uid_t uid;
    bool userChanged = false;
    int rc;
    VarFlagsState *pState = NULL;
    int result = EINVAL;

    /* create the vars utility instance */
    pState = (VarFlagsState *)calloc(1, sizeof( VarFlagsState ) );
    if ( pState != NULL )
    {
        /* get the user identifier */
        uid = getuid();

        /* get a handle to the variable server for transition events */
        pState->hVarServer = VARSERVER_Open();
        if ( pState->hVarServer != NULL )
        {
            /* Process Options */
            ProcessOptions( argC, argV, pState );

            if ( pState->username != NULL )
            {
                rc = SetUser( pState );
                if ( rc == EOK )
                {
                    userChanged = true;
                }
                else
                {
                    fprintf( stderr,
                            "Failed to set user to: %s rc=%d (%s)\n",
                            pState->username,
                            rc,
                            strerror(rc) );
                }
            }

            /* update the flags */
            result = varflags_Update( pState );
            if ( result == ENOENT )
            {
                fprintf( stderr, "WARN: No variables affected\n" );
            }

            if ( userChanged == true )
            {
                rc = setuid( uid );
            }

            /* close the variable server */
            VARSERVER_Close( pState->hVarServer );
            pState->hVarServer = NULL;
        }
    }

    return ( result == EOK ) ? 0 : 1;
}

/*============================================================================*/
/*  usage                                                                     */
/*!
    Display the vars utility usage

    The usage function dumps the application usage message
    to stderr.

    @param[in]
       cmdname
            pointer to the invoked command name

    @return none

==============================================================================*/
static void usage( char *cmdname )
{
    if( cmdname != NULL )
    {
        fprintf(stderr,
                "usage: %s [-n name] [-v] [-h]\n"
                " [-n name] : variable name search term\n"
                " [-f flagslist] : variable flags search term\n"
                " [-i instanceID]: instance identifier search term\n"
                " [-s flagslist] : set flags on matching vars\n"
                " [-c flagslist] : clear flags on matching vars\n"
                " [-v] : verbose output\n"
                " [-h] : display this help\n",
                cmdname );
    }
}

/*============================================================================*/
/*  ProcessOptions                                                            */
/*!
    Process the command line options

    The ProcessOptions function processes the command line options and
    populates the iotsend state object

    @param[in]
        argC
            number of arguments
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @param[in]
        pState
            pointer to the vars state

    @return none

==============================================================================*/
static int ProcessOptions( int argC,
                           char *argV[],
                           VarFlagsState *pState )
{
    int c;
    int result = EINVAL;
    const char *options = "hvn:f:i:u:s:c:";

    if( ( pState != NULL ) &&
        ( argV != NULL ) )
    {
        result = EOK;

        while( ( c = getopt( argC, argV, options ) ) != -1 )
        {
            switch( c )
            {
                case 'i':
                    pState->searchType |= QUERY_INSTANCEID;
                    pState->instanceID = atol(optarg);
                    break;

                case 'n':
                    pState->searchText = optarg;
                    pState->searchType |= QUERY_MATCH;
                    break;

                case 'f':
                    pState->searchType |= QUERY_FLAGS;
                    VARSERVER_StrToFlags( optarg, &pState->flags );
                    break;

                case 's':
                    VARSERVER_StrToFlags( optarg, &pState->setflags );
                    break;

                case 'c':
                    VARSERVER_StrToFlags( optarg, &pState->clearflags );
                    break;

                case 'u':
                    pState->username = optarg;
                    break;

                case 'v':
                    pState->verbose = true;
                    break;

                case 'h':
                    usage( argV[0] );
                    break;

                default:
                    break;

            }
        }
    }

    return result == EOK ? 0 : 1;
}

/*============================================================================*/
/*  SetUser                                                                   */
/*!
    Set the user

    Set the user to use to make the varserver query

    @param[in]
        pState
            pointer to the VarFlagsState object which contains the user name

    @retval EOK user set ok
    @retval EINVAL invalid arguments
    @retval other error from seteuid and setegid

==============================================================================*/
static int SetUser( VarFlagsState *pState )
{
    int result = EINVAL;
    struct passwd *pw;
    int rc;

    if ( pState != NULL )
    {
        if ( VARSERVER_SetGroup() == EOK )
        {
            pw = getpwnam( pState->username );
            if ( pw != NULL )
            {
                rc = seteuid( pw->pw_uid );
                if ( rc == EOK )
                {
                    result = VARSERVER_UpdateUser( pState->hVarServer );
                }
                else
                {
                    result = errno;
                }
            }
        }
    }

    return result;
}

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
static int varflags_Update( VarFlagsState *pState )
{
    int result = EINVAL;
    VarQuery query;
    char setFlagsStr[BUFSIZ];
    char clrFlagsStr[BUFSIZ];
    int rc;

    if ( pState != NULL )
    {
        /* set error return code assuming we find no matches */
        result = ENOENT;

        memset( &query, 0, sizeof( VarQuery ) );

        VARSERVER_FlagsToStr( pState->setflags,
                              setFlagsStr,
                              sizeof setFlagsStr );

        VARSERVER_FlagsToStr( pState->clearflags,
                              clrFlagsStr,
                              sizeof clrFlagsStr );

        query.type = pState->searchType;
        query.instanceID = pState->instanceID;
        query.match = pState->searchText;
        query.flags = pState->flags;

        rc = VAR_GetFirst( pState->hVarServer, &query, NULL );
        while ( rc == EOK )
        {
            if ( pState->setflags != 0 )
            {
                if ( pState->verbose )
                {
                    printf( "Setting flags '%s' on var %s: ",
                            setFlagsStr,
                            query.name );
                }

                result = VAR_SetFlags( pState->hVarServer,
                                    query.hVar,
                                    pState->setflags );
                if ( result != EOK )
                {
                    fprintf( stderr,
                             "Error setting flags '%s' on var %s\n",
                             setFlagsStr,
                             query.name );
                }

                if ( pState->verbose )
                {
                    printf("%s\n", (result == EOK ) ? "OK" : "FAILED" );
                }
            }
            else if ( pState->clearflags != 0 )
            {
                if ( pState->verbose )
                {
                    printf( "Clearing flags '%s' on var %s: ",
                            clrFlagsStr,
                            query.name );
                }

                result = VAR_ClearFlags( pState->hVarServer,
                                        query.hVar,
                                        pState->clearflags );
                if ( result != EOK )
                {
                    fprintf( stderr,
                             "Error clearing flags '%s' on var %s\n",
                             clrFlagsStr,
                             query.name );
                }

                if ( pState->verbose )
                {
                    printf("%s\n", (result == EOK ) ? "OK" : "FAILED" );
                }
            }
            else
            {
                result = ENOTSUP;
            }

            rc = VAR_GetNext( pState->hVarServer, &query, NULL );
        }
    }

    return result;
}

/*! @}
 * end of varflags group */
