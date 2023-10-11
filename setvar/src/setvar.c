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
 * @defgroup setvar setvar
 * @brief Set Variable value into the In-Memory Pub/Sub Key/Value store
 * @{
 */

/*============================================================================*/
/*!
@file setvar.c

    Set Variable

    The Set Variable Application sets the value for the specified
    variable in the variable server

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

/*! GetVar state object used to customize the behavior of the application */
typedef struct _set_var_state
{
    VARSERVER_HANDLE hVarServer;

    /*! name of the variable to set */
    char *varname;

    /*! value of the variable to set */
    char *value;

    /*! verbose mode */
    bool verbose;

    /*! user name */
    char *username;

} SetVarState;

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*==============================================================================
        Private function declarations
==============================================================================*/
static int SetUser( SetVarState *pState );
static int ProcessOptions( int argC,
                           char *argV[],
                           SetVarState *pState );
static void usage( char *name );
static int SetUser( SetVarState *pState );

/*============================================================================*/
/*  main                                                                      */
/*!
    Main entry point for the setvar application

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
    SetVarState state;
    int result = EINVAL;
    uid_t uid;
    bool userChanged = false;
    int rc;

    memset( &state, 0, sizeof(SetVarState));

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
                    fprintf( stderr,
                            "Failed to set user to: %s rc=%d (%s)\n",
                            state.username,
                            rc,
                            strerror(rc) );
                }
            }

            result = VAR_SetNameValue( state.hVarServer,
                                       state.varname,
                                       state.value );
            if( result != EOK )
            {
                fprintf(stderr, "SETVAR: %s\n", strerror( result ) );
            }
            else
            {
                printf("OK\n");
            }

            if ( userChanged == true )
            {
                setuid( uid );
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
            pointer to the SetVarState object

    @retval EOK ok to proceed
    @retval EINVAL do not proceed

==============================================================================*/
static int ProcessOptions( int argC,
                           char *argV[],
                           SetVarState *pState )
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
            pState->value = argV[optind];
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
        printf("usage: %s [-h] [-v] [-u <user>] varname value", name );
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
static int SetUser( SetVarState *pState )
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
 * end of setvar group */
