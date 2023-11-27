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
 * @defgroup varalias varalias
 * @brief Create an alias for an existing variable
 * @{
 */

/*============================================================================*/
/*!
@file varalias.c

    Create variable alias

    The VarAlias Application creates an alias for an existing variable.
    The new alias shares the variable storage and attributes of the
    existing variable, but has its own name, guid, and instance identifier.

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <pwd.h>
#include <varserver/varserver.h>

/*==============================================================================
        Private definitions
==============================================================================*/

/*! VarAlias state object used to customize the behavior of the application */
typedef struct _var_alias_state
{
    VARSERVER_HANDLE hVarServer;

    /*! name of the variable to alias */
    char *varname;

    /*! the new alias name */
    char *alias;

    /*! verbose mode */
    bool verbose;

    /*! user name */
    char *username;

} VarAliasState;

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*==============================================================================
        Private function declarations
==============================================================================*/
static int SetUser( VarAliasState *pState );
static int ProcessOptions( int argC,
                           char *argV[],
                           VarAliasState *pState );
static void usage( char *name );
static int SetUser( VarAliasState *pState );

/*============================================================================*/
/*  main                                                                      */
/*!
    Main entry point for the varalias application

    The main function starts the setvar application

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
    VarAliasState state;
    int result = EINVAL;
    uid_t uid;
    bool userChanged = false;
    int rc;
    VAR_HANDLE hVar;
    VAR_HANDLE hAlias;

    memset( &state, 0, sizeof(VarAliasState));

    result = ProcessOptions( argc, argv, &state );
    if ( result == EOK )
    {
        uid = getuid();

        /* get a handle to the VAR server */
        state.hVarServer = VARSERVER_Open();
        if( state.hVarServer != NULL )
        {
            if ( state.username != NULL )
            {
                rc = SetUser( &state );
                if ( rc == EOK )
                {
                    userChanged = true;
                }
                else
                {
                    if ( state.verbose == true )
                    {
                        fprintf( stderr,
                                "Failed to set user to: %s rc=%d (%s)\n",
                                state.username,
                                rc,
                                strerror(rc) );
                    }
                }
            }

            result = ENOENT;
            hVar = VAR_FindByName( state.hVarServer, state.varname );
            if ( hVar != VAR_INVALID )
            {
                result = VAR_Alias( state.hVarServer,
                                    hVar,
                                    state.alias,
                                    &hAlias );
                if ( state.verbose == true )
                {
                    if ( result != EOK )
                    {
                        fprintf(stderr, "VARALIAS: %s\n", strerror( result ) );
                    }
                    else
                    {
                        printf("OK\n");
                    }
                }
            }
            else
            {
                if ( state.verbose == true )
                {
                    fprintf(stderr, "VARALIAS: %s\n", strerror( result ) );
                }
            }

            if ( userChanged == true )
            {
                rc = setuid( uid );
            }

            /* close the variable server */
            VARSERVER_Close( state.hVarServer );
        }
    }

    return result == EOK ? 0 : 1;
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
            pointer to the VarAliasState object

    @retval EOK ok to proceed
    @retval EINVAL do not proceed

==============================================================================*/
static int ProcessOptions( int argC,
                           char *argV[],
                           VarAliasState *pState )
{
    int c;
    int result = EINVAL;
    const char *options = "hvu:";
    int errcount = 0;
    bool display_help = false;

    if( ( pState != NULL ) &&
        ( argV != NULL ) )
    {
        while( ( c = getopt( argC, argV, options ) ) != -1 )
        {
            switch( c )
            {
                case 'v':
                    pState->verbose = true;
                    break;

                case 'u':
                    pState->username = optarg;
                    break;

                case 'h':
                    display_help = true;
                    break;

                default:
                    errcount++;
                    break;

            }
        }

        if ( optind < argC - 1)
        {
            pState->varname = argV[optind++];
            pState->alias = argV[optind];
            result = EOK;
        }
        else
        {
            errcount++;
        }

        if ( ( display_help == true ) ||
             ( errcount > 0 ) )
        {
            usage( argV[0] );
            errcount++;
        }

        if ( errcount == 0 )
        {
            result = EOK;
        }
    }

    return result;
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
        printf("usage: %s [-h] [-v] [-u <user>] varname alias\n", name );
        printf("-h : display this help\n");
        printf("-v : enable verbose (debugging) output\n");
        printf("-u : set user name\n");
    }
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
static int SetUser( VarAliasState *pState )
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
 * end of varalias group */
