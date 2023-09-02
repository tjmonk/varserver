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
 * @defgroup watchvar watchvar
 * @brief Watch a Variable from the RealTime In-Memory Pub/Sub Key/Value store
 * @{
 */

/*============================================================================*/
/*!
@file watchvar.c

    Watch Variable

    The Watch Variable Application watches a variable server variable
    and displays its value when it changes.

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <varserver/varserver.h>

/*==============================================================================
        Private definitions
==============================================================================*/

/*! WatchVar state object used to customize the behavior of the application */
typedef struct _watch_var_state
{
    VARSERVER_HANDLE hVarServer;

    /*! name of the variable to watch */
    char *varname;

    /*! verbose mode */
    bool verbose;

    /*! suppress newline */
    bool suppressNewline;
} WatchVarState;

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*==============================================================================
        Private function declarations
==============================================================================*/

int main( int argc, char **argv );
static int ProcessOptions( int argc, char **argv, WatchVarState *pState );
static void usage( char *name );
static int WatchVar( WatchVarState *pState );
static int PrintVar( WatchVarState *pState, VAR_HANDLE hVar, int fd );

/*==============================================================================
        Function definitions
==============================================================================*/

/*============================================================================*/
/*  main                                                                      */
/*!
    Main entry point for the getvar application

    The main function starts the getvar application

    @param[in]
        argc
            number of arguments on the command line
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @return none

==============================================================================*/
int main(int argc, char **argv)
{
    VARSERVER_HANDLE hVarServer = NULL;
    VAR_HANDLE hVar;
    int result;
    WatchVarState state;
    int fd;

    /*! clear the state object */
    memset( &state, 0, sizeof( WatchVarState ) );

    /*! process the command line options */
    if( ProcessOptions( argc, argv, &state ) == EOK )
    {
        /* get a handle to the VAR server */
        state.hVarServer = VARSERVER_Open();
        if( state.hVarServer != NULL )
        {
            /* process the vars query */
            result = WatchVar( &state );

            /* close the variable server */
            VARSERVER_Close( state.hVarServer );
        }
        else
        {
            fprintf(stderr, "Unable to open variable server\n");
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessOptions                                                            */
/*!
    Process command line options

    The ProcessOptions function gathers the command line options
    into the WatchVarState object and displays usage information
    if the options are not correct.

    Supported options are:

    -v : enable verbose output
    -h : display help
    -N : no newline

    @param[in]
        argc
            number of arguments on the command line
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @param[in]
        pState
            pointer to the WatchVarState object

    @retval EOK options processed successfully
    @retval EINVAL an error occurred

==============================================================================*/
static int ProcessOptions( int argc, char **argv, WatchVarState *pState )
{
    const char *options = "hvn";
    int c;
    int errcount = 0;
    bool display_help = false;

    if( ( pState != NULL ) &&
        ( argv != NULL ) )
    {
        if( argc < 2 )
        {
            errcount ++;
        }
        else
        {
            while( ( c = getopt( argc, argv, options ) ) != -1 )
            {
                switch ( c )
                {
                    case 'h':
                        display_help = true;
                        break;

                    case 'v':
                        pState->verbose = true;
                        break;

                    case 'n':
                        pState->suppressNewline = true;
                        break;

                    default:
                        errcount++;
                        break;

                }
            }

            if( optind < argc )
            {
                /* get the variable name */
                pState->varname = argv[optind];
            }
            else
            {
                errcount++;
            }
        }


        if( ( display_help == true ) ||
            ( errcount > 0 ) )
        {
            usage( argv[0] );
        }
    }
    else
    {
        errcount++;
    }

    return ( errcount == 0 ) ? EOK : EINVAL;
}

/*============================================================================*/
/*  usage                                                                     */
/*!
    Display the usage information

    The usage function describes the command line options on the
    standard output stream.

    @param[in]
        name
            pointer to the application name

==============================================================================*/
static void usage( char *name )
{
    if( name != NULL )
    {
        printf("usage: %s [-h] [-v] [-n] <variable name>\n", name );
        printf("-h : display this help\n");
        printf("-v : enable verbose (debugging) output\n");
        printf("-n : suppress newline\n");
    }
}

/*============================================================================*/
/*  WatchVar                                                                  */
/*!
    Watch the variable for changes

    The WatchVar function waits for a MODIFIED signal from the variable
    server and then prints the value of the variable

    @param[in]
        pState
            pointer to the WatchVarState object containing the output options

    @retval EINVAL invalid arguments
    @retval EOK output was successful
    @retval ENOENT variable not found

==============================================================================*/
static int WatchVar( WatchVarState *pState )
{
    int result = EINVAL;
    VAR_HANDLE hVar;
    int sig;
    int sigval;
    int rc;

    if( pState != NULL )
    {
        /* get the handle to the specified variable */
        hVar = VAR_FindByName( pState->hVarServer, pState->varname );
        if ( hVar != VAR_INVALID )
        {
            rc = VAR_Notify( pState->hVarServer, hVar, NOTIFY_MODIFIED );
            if ( rc == EOK )
            {
                while ( 1 )
                {
                    sig = VARSERVER_WaitSignal( pState->hVarServer, &sigval );
                    if ( sig == SIG_VAR_MODIFIED )
                    {
                        if ( sigval == hVar )
                        {
                            if( pState->suppressNewline == true )
                            {
                                dprintf( STDOUT_FILENO, "\033[H\033[J" );
                            }

                            dprintf(STDOUT_FILENO, "%s: ", pState->varname );
                            VAR_Print( pState->hVarServer, hVar, STDOUT_FILENO);
                            if ( pState->suppressNewline == false )
                            {
                                dprintf(STDOUT_FILENO, "\n");
                            }
                        }
                    }
                }
            }
            else
            {
                printf("rc=%d\n", rc);
            }
        }
    }

    return result;
}

/*! @}
 * end of watchvar group */
