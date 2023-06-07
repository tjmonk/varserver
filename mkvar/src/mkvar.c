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
 * @defgroup mkvar mkvar
 * @brief Make a Variable in the RealTime In-Memory Pub/Sub Key/Value store
 * @{
 */

/*==========================================================================*/
/*!
@file mkvar.c

    Make Variable

    The Make Variable Application creates a new variable in the variable
    server

*/
/*==========================================================================*/

/*============================================================================
        Includes
============================================================================*/

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "varserver.h"

/*============================================================================
        Private definitions
============================================================================*/

/*! MakeVarState object used to customize the behavior of the application */
typedef struct _make_var_state
{
    /*! handle to the variable server */
    VARSERVER_HANDLE hVarServer;

    /*! variable information used to create the variable */
    VarInfo variableInfo;

    /*! variable value */
    char *value;

    /*! verbose mode */
    bool verbose;

} MakeVarState;

/*============================================================================
        Private file scoped variables
============================================================================*/

/*============================================================================
        Private function declarations
============================================================================*/

int main( int argc, char **argv );
static int ProcessOptions( int argc, char **argv, MakeVarState *pState );
static void usage( char *name );
static int ProcessQuery( MakeVarState *pState );
static int MakeVar( MakeVarState *pState );

/*============================================================================
        Function definitions
============================================================================*/

/*==========================================================================*/
/*  main                                                                    */
/*!
    Main entry point for the mkvar application

    The main function starts the mkvar application

    @param[in]
        argc
            number of arguments on the command line
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @return none

============================================================================*/
int main(int argc, char **argv)
{
    VARSERVER_HANDLE hVarServer = NULL;
    VAR_HANDLE hVar;
    int result;
    MakeVarState state;

    /*! clear the state object */
    memset( &state, 0, sizeof( MakeVarState ) );

    /*! process the command line options */
    if( ProcessOptions( argc, argv, &state ) == EOK )
    {
        /* get a handle to the VAR server */
        state.hVarServer = VARSERVER_Open();
        if( state.hVarServer != NULL )
        {
            /* process the vars query */
            result = MakeVar( &state );

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

/*==========================================================================*/
/*  ProcessOptions                                                          */
/*!
    Process command line options

    The ProcessOptions function gathers the command line options
    into the GetVarState object and displays usage information
    if the options are not correct.

    Supported options are:

    -n : variable name
    -i : instance identifier
    -v : variable value
    -g : variable GUID
    -f : variable flags
    -F : variable format
    -t : variable type
    -T : variable tag specifiers
    -l : variable length (string variables)

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

============================================================================*/
static int ProcessOptions( int argc, char **argv, MakeVarState *pState )
{
    const char *options = "n:i:v:g:f:F:t:T:l:";
    int c;
    int errcount = 0;
    size_t len;
    VarType type;
    int rc;

    if( ( pState != NULL ) &&
        ( argv != NULL ) )
    {
        if( argc < 2 )
        {
            errcount ++;
        }
        else
        {
            /* set up defaults to create a 256 char string variable */
            pState->variableInfo.var.type = VARTYPE_STR;
            pState->variableInfo.var.len = 256;

            while( ( c = getopt( argc, argv, options ) ) != -1 )
            {
                switch ( c )
                {
                    case 'n':
                        if ( strlen( optarg ) <= MAX_NAME_LEN )
                        {
                            strcpy( pState->variableInfo.name, optarg );
                        }
                        else
                        {
                            fprintf(stderr, "illegal variable name length\n");
                            errcount++;
                        }
                        break;

                    case 'v':
                        pState->value = optarg;
                        break;

                    case 'g':
                        pState->variableInfo.guid = strtol( optarg, NULL, 0 );
                        break;

                    case 'i':
                        pState->variableInfo.instanceID = strtol( optarg, NULL, 0 );
                        break;

                    case 'f':
                        rc = VARSERVER_StrToFlags( optarg,
                                                   &pState->variableInfo.flags );
                        if ( rc != EOK )
                        {
                            fprintf( stderr, "error converting flags string\n");
                            errcount++;
                        }
                        break;

                    case 'F':
                        if ( strlen( optarg ) < MAX_FORMATSPEC_LEN )
                        {
                            strcpy( pState->variableInfo.formatspec, optarg );
                        }
                        else
                        {
                            fprintf( stderr, "Illegal format spec length\n");
                            errcount++;
                        }
                        break;


                    case 'T':
                        if ( strlen( optarg ) < MAX_TAGSPEC_LEN )
                        {
                            strcpy( pState->variableInfo.tagspec, optarg );
                        }
                        else
                        {
                            fprintf( stderr, "Illegal tag spec length\n");
                            errcount++;
                        }
                        break;

                    case 't':
                        rc = VARSERVER_TypeNameToType( optarg, &type );
                        if ( rc == EOK )
                        {
                            pState->variableInfo.var.type = type;
                        }
                        else
                        {
                            fprintf( stderr, "Illegal type\n");
                            errcount++;
                        }
                        break;

                    case 'l':
                        len = strtoul( optarg, NULL, 0);
                        if ( ( len > 0 ) && ( len <= 16384 ) )
                        {
                            pState->variableInfo.var.len = len;
                        }
                        else
                        {
                            fprintf(stderr, "Illegal length\n" );
                            errcount++;
                        }
                        break;

                    default:
                        fprintf(stderr, "invalid option\n");
                        errcount++;
                        break;

                }
            }

            if( optind < argc )
            {
                /* get the variable name */
                if ( strlen( argv[optind] ) <= MAX_NAME_LEN )
                {
                    strcpy( pState->variableInfo.name, argv[optind] );
                }
                else
                {
                    fprintf(stderr, "illegal variable name length\n");
                    errcount++;
                }
            }
        }

        if( errcount > 0 )
        {
            printf("errcount=%d\n", errcount);
            usage( argv[0] );
        }
        else
        {
            if ( pState->value != NULL )
            {
                rc = VAROBJECT_CreateFromString( pState->value,
                                                 pState->variableInfo.var.type,
                                                 &pState->variableInfo.var,
                                                 0 );
                if ( rc != EOK )
                {
                    fprintf( stderr, "Cannot assign variable value\n" );
                    errcount++;
                }
            }
        }
    }
    else
    {
        errcount++;
    }

    return ( errcount == 0 ) ? EOK : EINVAL;
}

/*==========================================================================*/
/*  usage                                                                   */
/*!
    Display the usage information

    The usage function describes the command line options on the
    standard output stream.

    @param[in]
        name
            pointer to the application name

============================================================================*/
static void usage( char *name )
{
    if( name != NULL )
    {
        printf("usage: %s [-h] [-v] [-c] [-N] [-t] [-n <num>] [-o <outfile> ]"
               "[-w <wait time>] <variable name>\n\n", name );
        printf("-n : variable name\n");
        printf("-i : variable instance identifier\n");
        printf("-v : variable initial value\n");
        printf("-g : variable GUID\n");
        printf("-f : variable flags\n");
        printf("-F : variable format specifier\n");
        printf("-t : variable type\n");
        printf("-T : variable tags\n");
        printf("-l : variable length\n");
    }
}

/*==========================================================================*/
/*  MakeVar                                                                 */
/*!
    Process the make variable request

    The MakeVar function create a new variable in the variable server

    @param[in]
        pState
            pointer to the MakeVarState object containing the variable
            information

    @retval EINVAL invalid arguments
    @retval EOK variable creation was successful

============================================================================*/
static int MakeVar( MakeVarState *pState )
{
    int result = EINVAL;

    if( pState != NULL )
    {
        /*! request the variable server to create the variable */
        result = VARSERVER_CreateVar( pState->hVarServer, &(pState->variableInfo) );
    }

    return result;
}


/*! @}
 * end of mkvar group */
