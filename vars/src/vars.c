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
    VarFlags flags;

    /*! type of search - bitfield */
    int searchType;

    /*! instance identifier */
    uint32_t instanceID;

} VarsState;

/*==============================================================================
       Function declarations
==============================================================================*/
static void usage( char *cmdname );
static int ProcessOptions( int argC,
                           char *argV[],
                           VarsState *pState );

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
    pState = NULL;

    /* create the vars utility instance */
    pState = (VarsState *)calloc(1, sizeof( VarsState ) );
    if ( pState != NULL )
    {
        pState->fd = STDOUT_FILENO;

        /* get a handle to the variable server for transition events */
        pState->hVarServer = VARSERVER_Open();
        if ( pState->hVarServer != NULL )
        {
            /* Process Options */
            ProcessOptions( argC, argV, pState );

            /* make the query */

            (void)VARQUERY_Search( pState->hVarServer,
                                   pState->searchType,
                                   pState->searchText,
                                   pState->instanceID,
                                   pState->flags,
                                   pState->fd );

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
                " [-f flagslist] : variable flags search term\n"
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

    @return none

==============================================================================*/
static int ProcessOptions( int argC,
                           char *argV[],
                           VarsState *pState )
{
    int c;
    int result = EINVAL;
    const char *options = "hvn:f:i:";

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

                case 'v':
                    pState->searchType |= QUERY_SHOWVALUE;
                    break;

                case 'n':
                    pState->searchText = optarg;
                    pState->searchType |= QUERY_MATCH;
                    break;

                case 'f':
                    pState->searchType |= QUERY_FLAGS;
                    VARSERVER_StrToFlags( optarg, &pState->flags );
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

