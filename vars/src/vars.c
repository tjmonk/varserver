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
 * @defgroup vars vars
 * @brief Variable Query utility
 * @{
 */

/*============================================================================*/
/*!
@file vars.c

    Variable Query Utility

    The Vars utility can be used to query the variable server
    via the libvarquery library to obtain a list of variables
    and their values which match the specified search criteria.

    Variables can be searched using their name, instance identifier,
    tags, and flags.

    Search options are specified via command line arguments to the vars
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

/*==============================================================================
       Type Definitions
==============================================================================*/
typedef struct _varsState
{
    /*! handle to the variable server */
    VARSERVER_HANDLE hVarServer;

    /*! search text */
    char *searchText;

    /*! output file descriptor */
    int fd;

    /*! flags to search for */
    uint32_t flags;

    /*! type of search - bitfield */
    int searchType;

    /*! instance identifier */
    uint32_t instanceID;

    /*! user name */
    char *username;

    /* tagspec */
    char *tagspec;

} VarsState;

/*==============================================================================
       Function declarations
==============================================================================*/
static void usage( char *cmdname );
static int ProcessOptions( int argC,
                           char *argV[],
                           VarsState *pState );
static int SetUser( VarsState *pState );

/*==============================================================================
       Definitions
==============================================================================*/

/*==============================================================================
      File Scoped Variables
==============================================================================*/
VarsState *pState;

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

    pState = NULL;

    /* create the vars utility instance */
    pState = (VarsState *)calloc(1, sizeof( VarsState ) );
    if ( pState != NULL )
    {
        pState->fd = STDOUT_FILENO;

        /* get the user identifier */
        uid = getuid();

        /* get a handle to the variable server for transition events */
        pState->hVarServer = VARSERVER_Open();
        if ( pState->hVarServer != NULL )
        {
            /* Process Options */
            rc = ProcessOptions( argC, argV, pState );
            if ( rc == 0 )
            {

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

                /* make the query */
                (void)VARQUERY_Search( pState->hVarServer,
                                    pState->searchType,
                                    pState->searchText,
                                    pState->tagspec,
                                    pState->instanceID,
                                    pState->flags,
                                    pState->fd );

                if ( userChanged == true )
                {
                    rc = setuid( uid );
                }
            }

            /* close the variable server */
            VARSERVER_Close( pState->hVarServer );
            pState->hVarServer = NULL;
        }
    }

    return 0;
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
                " [-r regex] : variable name search by a regular expression\n"
                " [-f flagslist] : variable flags search term\n"
                " [-F flagslist]: negative variable flags search. Supercedes -f\n"
                " [-i instanceID]: instance identifier search term\n"
                " [-h] : display this help\n"
                " [-v] : output values\n",
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

    @return 0 if processing was successful, 1 otherwise

==============================================================================*/
static int ProcessOptions( int argC,
                           char *argV[],
                           VarsState *pState )
{
    int c;
    int result = EOK;
    const char *options = "hvn:r:f:F:i:u:t:";

    if( ( pState != NULL ) &&
        ( argV != NULL ) )
    {
        while( ( c = getopt( argC, argV, options ) ) != -1 )
        {
            switch( c )
            {
                case 'i':
                    pState->searchType |= QUERY_INSTANCEID;
                    pState->instanceID = atol(optarg);
                    break;

                case 'v':
                    pState->searchType |= QUERY_SHOWVALUE;
                    break;

                case 'n':
                    pState->searchText = optarg;
                    pState->searchType |= QUERY_MATCH;
                    break;

                case 'r':
                    pState->searchText = optarg;
                    pState->searchType |= QUERY_REGEX;
                    break;

                case 'f':
                    pState->searchType |= QUERY_FLAGS;
                    VARSERVER_StrToFlags( optarg, &pState->flags );
                    break;

                case 'F':
                    pState->searchType |= QUERY_FLAGS | QUERY_NEGATE_FLAGS;
                    VARSERVER_StrToFlags( optarg, &pState->flags );
                    break;

                case 't':
                    pState->searchType |= QUERY_TAGS;
                    pState->tagspec = optarg;
                    break;

                case 'u':
                    pState->username = optarg;
                    break;

                case 'h':
                    usage( argV[0] );
                    result = EINVAL;
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
            pointer to the VarsState object which contains the user name

    @retval EOK user set ok
    @retval EINVAL invalid arguments
    @retval other error from seteuid and setegid

==============================================================================*/
static int SetUser( VarsState *pState )
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

/*! @}
 * end of vars group */
