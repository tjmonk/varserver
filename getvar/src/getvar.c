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
 * @defgroup getvar getvar
 * @brief Get Variable from the RealTime In-Memory Pub/Sub Key/Value store
 * @{
 */

/*============================================================================*/
/*!
@file getvar.c

    Get Variable

    The Get Variable Application gets the value for the specified
    variable from the variable server

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
#include <inttypes.h>
#include <varserver/varserver.h>

/*==============================================================================
        Private definitions
==============================================================================*/

/*! GetVar state object used to customize the behavior of the application */
typedef struct _get_var_state
{
    VARSERVER_HANDLE hVarServer;

    /*! name of the variable to get */
    char *varname;

    /*! verbose mode */
    bool verbose;

    /*! number of times to get the variable */
    uint32_t n;

    /*! flag to indicate the query should repeat forever */
    bool repeatForever;

    /*! delay (in milliseconds) after each retrieval */
    uint32_t delayMS;

    /*! output file name */
    char *outfile;

    /*! show query count */
    bool showQueryCount;

    /*! query counter */
    uint32_t queryCounter;

    /*! suppress newline */
    bool suppressNewline;

    /*! timing test mode */
    bool timingTest;

} GetVarState;

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*==============================================================================
        Private function declarations
==============================================================================*/

int main( int argc, char **argv );
static int ProcessOptions( int argc, char **argv, GetVarState *pState );
static void usage( char *name );
static int GetOutputFileDescriptor( GetVarState *pState );
static int ProcessQuery( GetVarState *pState );
static int PrintVar( GetVarState *pState, VAR_HANDLE hVar, int fd );
static int TimingTest( GetVarState *pState, VAR_HANDLE hVar );

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
    int result = EINVAL;
    GetVarState state;

    /*! clear the state object */
    memset( &state, 0, sizeof( GetVarState ) );

    /*! initialize the operating state */
    state.n = 1;

    /*! process the command line options */
    if( ProcessOptions( argc, argv, &state ) == EOK )
    {
        /* get a handle to the VAR server */
        state.hVarServer = VARSERVER_Open();
        if( state.hVarServer != NULL )
        {
            /* process the vars query */
            result = ProcessQuery( &state );

            /* close the variable server */
            VARSERVER_Close( state.hVarServer );
        }
        else
        {
            fprintf(stderr, "Unable to open variable server\n");
        }
    }

    return result == EOK ? 0 : 1;
}

/*============================================================================*/
/*  ProcessOptions                                                            */
/*!
    Process command line options

    The ProcessOptions function gathers the command line options
    into the GetVarState object and displays usage information
    if the options are not correct.

    Supported options are:

    -v : enable verbose output
    -h : display help
    -o : specify an output file
    -n : specify the number of times to get the variable
    -w : specifies the wait time (in milliseconds) between queries
    -r : repeat forever
    -c : show query count
    -t : timing test mode
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
            pointer to the GetVarState object

    @retval EOK options processed successfully
    @retval EINVAL an error occurred

==============================================================================*/
static int ProcessOptions( int argc, char **argv, GetVarState *pState )
{
    const char *options = "hvo:n:w:rcNt";
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

                    case 'o':
                        pState->outfile = optarg;
                        break;

                    case 'w':
                        pState->delayMS = strtoul( optarg, NULL, 0 );
                        break;

                    case 'n':
                        pState->n = strtoul( optarg, NULL, 0 );
                        break;

                    case 'r':
                        pState->repeatForever = true;
                        break;

                    case 'c':
                        pState->showQueryCount = true;
                        break;

                    case 'N':
                        pState->suppressNewline = true;
                        break;

                    case 't':
                        pState->timingTest = true;
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
        printf("usage: %s [-h] [-v] [-c] [-N] [-t] [-n <num>] [-o <outfile> ]"
               "[-w <wait time>] <variable name>\n\n", name );
        printf("-h : display this help\n");
        printf("-v : enable verbose (debugging) output\n");
        printf("-n : specify the number of times to make the query\n");
        printf("-w : specify the wait time between queries\n");
        printf("-o : specify an output file\n");
        printf("-t : enable timing test mode\n");
        printf("-r : repeat forever\n");
        printf("-c : show query count\n");
        printf("-N : suppress newline\n");
    }
}

/*============================================================================*/
/*  GetOutputFileDescriptor                                                   */
/*!
    Get the output file descriptor for the query output

    The GetOutputFileDescriptor function gets the file descriptor
    for the output file specified in the GetVarState object, or returns
    the file descriptor for the standard output if the output file
    is not specified.  If the state object is invalid, the function
    will return -1.

    @param[in]
        pState
            pointer to the GetVarState object containing the output file name

    @retval file descriptor for the output file
    @retval STDOUT_FILENO if no output file was specified
    @retval -1 if the specified GetVarState object is invalid

==============================================================================*/
static int GetOutputFileDescriptor( GetVarState *pState )
{
    int fd = -1;

    if( pState != NULL )
    {
        /* output to standard output by default */
        fd = STDOUT_FILENO;

        /* open the output file */
        if( pState->outfile != NULL )
        {
            fd = open( pState->outfile, O_WRONLY );
            if( fd == -1 )
            {
                printf( "Unable to open %s, redirecting to standard output\n",
                        pState->outfile );
            }
        }
    }

    return fd;
}

/*============================================================================*/
/*  ProcessQuery                                                              */
/*!
    Process the variable query request

    The ProcessQuery function gets the file descriptor
    for the output file specified in the GetVarState object, and prints
    the value of the requested variable to the output file descriptor.

    If specified by the GetVarState object, the query may be repeated a
    number of times.

    If specified by the GetVarState object, there may be a delay between
    queries.

    @param[in]
        pState
            pointer to the GetVarState object containing the output options

    @retval EINVAL invalid arguments
    @retval EOK output was successful
    @retval ENOENT variable not found

==============================================================================*/
static int ProcessQuery( GetVarState *pState )
{
    int result = EINVAL;
    VAR_HANDLE hVar;
    int fd;
    bool done = false;

    if( pState != NULL )
    {
        /* get the output file descriptor for the query results */
        fd = GetOutputFileDescriptor( pState );

        if( fd != -1 )
        {
            /* get the handle to the specified variable */
            hVar = VAR_FindByName( pState->hVarServer, pState->varname );

            /* repeat until done */
            while( !done )
            {
                /* output the query counter if requested */
                if( pState->showQueryCount == true )
                {
                    dprintf(fd, "%"PRIu32",", ++pState->queryCounter );
                }

                if ( pState->timingTest == true )
                {
                    result = TimingTest( pState, hVar );
                }
                else
                {
                    result = PrintVar( pState, hVar, fd );
                }

                done = (result == EAGAIN) ? false : true;
            }

            if( fd != STDOUT_FILENO )
            {
                /* close the opened file descriptor */
                close( fd );
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  PrintVar                                                                  */
/*!
    Query and output the variable N times

    The PrintVar function prints out the specified variable
    value N times, with a possible delay between each variable
    query.

    If specified by the GetVarState object, the query may be repeated a
    number of times.

    If specified by the GetVarState object, there may be a delay between
    queries.

    @param[in]
        pState
            pointer to the GetVarState object containing the output options

    @param[in]
        hVar
            handle to the variable to be queried

    @param[in]
        fd
            output file descriptor

    @retval EINVAL invalid arguments
    @retval EOK output was successful
    @retval EAGAIN output count not reached, invoke function again
    @retval ENOENT variable not found

==============================================================================*/
static int PrintVar( GetVarState *pState, VAR_HANDLE hVar, int fd )
{
    int result = EINVAL;

    if( (  pState != NULL ) &&
        (  hVar != VAR_INVALID ) )
    {
        /* print the output variable */
        result = VAR_Print( pState->hVarServer, hVar, fd );
        if( result != EOK )
        {
            fprintf(stderr, "GETVAR: %s\n", strerror( result ) );
        }
        else
        {
            /* output a newline unless we have a request to
             * suppress the newline */
            if( pState->suppressNewline == false )
            {
                printf("\n");
            }
        }

        if( result == EOK )
        {
            if ( pState->delayMS != 0 )
            {
                usleep( pState->delayMS * 1000 );
            }

            /* check if we are repeating forever */
            if( pState->repeatForever == false )
            {
                /* decrement the repeat count until it reaches zero */
                result = ( --pState->n == 0 ) ? EOK : EAGAIN;
            }
            else
            {
                result = EAGAIN;
            }
        }
    }

    return result;

}

/*============================================================================*/
/*  TimingTest                                                                */
/*!
    Query the variable N times for a timing test

    The TimingTest function queries the specified variable but does not
    print out the value.

    If specified by the GetVarState object, the query may be repeated a
    number of times.

    If specified by the GetVarState object, there may be a delay between
    queries.

    @param[in]
        pState
            pointer to the GetVarState object containing the output options

    @param[in]
        hVar
            handle to the variable to be queried

    @retval EINVAL invalid arguments
    @retval EOK query was successful and completed
    @retval EAGAIN query count not reached, invoke function again
    @retval ENOENT variable not found

==============================================================================*/
static int TimingTest( GetVarState *pState, VAR_HANDLE hVar )
{
    int result = EINVAL;
    VarObject obj;
    char buffer[BUFSIZ];

    /* specify the receive buffer and buffer length */
    obj.val.str = buffer;
    obj.len = BUFSIZ;

    if( (  pState != NULL ) &&
        (  hVar != VAR_INVALID ) )
    {
        /* query the variable */
        result = VAR_Get( pState->hVarServer, hVar, &obj );
        if( result != EOK )
        {
            fprintf(stderr, "GETVAR: %s\n", strerror( result ) );
        }
        else
        {
            if ( pState->delayMS != 0 )
            {
                usleep( pState->delayMS * 1000 );
            }

            /* check if we are repeating forever */
            if( pState->repeatForever == false )
            {
                /* decrement the repeat count until it reaches zero */
                result = ( --pState->n == 0 ) ? EOK : EAGAIN;
            }
            else
            {
                result = EAGAIN;
            }
        }
    }

    return result;

}

/*! @}
 * end of getvar group */
