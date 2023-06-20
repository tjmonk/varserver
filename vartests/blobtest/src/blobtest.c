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
 * @defgroup blobtest Blob Test
 * @brief Blob Testing Variable Server Client
 * @{
 */

/*============================================================================*/
/*!
@file blobtest.c

    Blob handler client

    The blobtest blob handler client provides a demonstration for blob
    handling and CALC, PRINT, and MODIFIED signal handling for blob objects.

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
#include <unistd.h>
#include <syslog.h>
#include <fcntl.h>
#include <varserver/varserver.h>

/*==============================================================================
        Private definitions
==============================================================================*/

/*! Blobtest state */
typedef struct blobtestState
{
    /*! variable server handle */
    VARSERVER_HANDLE hVarServer;

    /*! verbose flag */
    bool verbose;

    /*! show program usage */
    bool usage;

    /*! client gets the variable */
    bool get;

    /*! client sets the variable */
    bool set;

    /*! client handles calculation of variable */
    bool calc;

    /*! client handles rendering of variable */
    bool print;

    /*! client waits for variable to be modified */
    bool wait;

    /*! quiet mode - suppress all output */
    bool quiet;

    /*! name of blob variable */
    char *varname;

    /*! number of times to get or set */
    size_t n;

    /* delay in milliseconds between sets/gets */
    int delay;

    /*! count render actions*/
    size_t rendercount;

    /*! count calc actions */
    size_t calccount;

    /*! modified count */
    size_t modifiedcount;

} BlobTestState;

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! BlobTest State object */
BlobTestState state;

/*==============================================================================
        Private function declarations
==============================================================================*/

void main(int argc, char **argv);
static void usage( char *cmdname );
static int ProcessOptions( int argC, char *argV[], BlobTestState *pState );
static int GetRandomData( BlobTestState *pState, VarObject *pVarObject );
static int PrintBlobObj( BlobTestState *pState, VarObject *pObj, int fd );
static void SetupTerminationHandler( void );
static void TerminationHandler( int signum, siginfo_t *info, void *ptr );


/*==============================================================================
        Private function definitions
==============================================================================*/

/*============================================================================*/
/*  main                                                                      */
/*!
    Main entry point for the execvars application

    The main function starts the execvars application

    @param[in]
        argc
            number of arguments on the command line
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @return none

==============================================================================*/
void main(int argc, char **argv)
{
    VAR_HANDLE hVar;
    VAR_HANDLE hTestVar;

    int sigval;
    int fd;
    int sig;
    char buf[BUFSIZ];
    VarObject obj;
    size_t len;

    /* clear the var object */
    memset( &obj, 0, sizeof( VarObject ) );

    /* clear the state object */
    memset( &state, 0, sizeof( BlobTestState ) );

    state.varname = "/sys/test/blob";
    state.n = 1;
    state.quiet = false;

    if( ( argc < 2 ) ||
        ( state.usage == true ) )
    {
        usage( argv[0] );
        exit( 1 );
    }

    /* process the command line options */
    ProcessOptions( argc, argv, &state );

    /* set up an abnormal termination handler */
    SetupTerminationHandler();

    /* get a handle to the VAR server */
    state.hVarServer = VARSERVER_Open();
    if( state.hVarServer != NULL )
    {
        hTestVar = VAR_FindByName( state.hVarServer, state.varname );
        if ( hTestVar != VAR_INVALID )
        {
            /* get the length */
            VAR_GetLength( state.hVarServer, hTestVar, &len );

            /* set up the VarObject */
            obj.type = VARTYPE_BLOB;
            obj.len = len;
            obj.val.blob = malloc( len );

            /* var set loop */
            while ( ( state.set == true ) && ( state.n > 0 ) )
            {
                if ( state.verbose == true )
                {
                    printf("Setting %s\n", state.varname );
                }

                GetRandomData( &state, &obj );
                VAR_Set( state.hVarServer, hTestVar, &obj );
                state.n--;

                if ( state.delay > 0 )
                {
                    usleep( state.delay * 1000 );
                }
            }

            /* var get loop */
            while ( ( state.get == true ) && ( state.n > 0 ) )
            {
                if ( state.verbose == true )
                {
                    printf("Getting %s\n", state.varname );
                }

                VAR_Get( state.hVarServer, hTestVar, &obj );
                state.n--;

                if ( state.verbose == true )
                {
                    PrintBlobObj( &state, &obj, STDOUT_FILENO );
                    printf("\n");
                }

                if ( state.delay > 0 )
                {
                    usleep( state.delay * 1000 );
                }
            }

            /* tell varserver we will handle PRINT and CALC notifications
               for the /sys/test/blob variable */
            if ( state.print == true )
            {
                VAR_Notify( state.hVarServer, hTestVar, NOTIFY_PRINT );
            }

            if ( state.calc == true )
            {
                VAR_Notify( state.hVarServer, hTestVar, NOTIFY_CALC );
            }

            if ( state.wait == true )
            {
                VAR_Notify( state.hVarServer, hTestVar, NOTIFY_MODIFIED );
            }
        }

        while( state.print || state.calc || state.wait )
        {
            /* wait for a signal from the variable server */
            sig = VARSERVER_WaitSignal( &sigval );

            if( sig == SIG_VAR_PRINT )
            {
                /* open a print session */
                VAR_OpenPrintSession( state.hVarServer,
                                      sigval,
                                      &hVar,
                                      &fd );

                if ( hVar == hTestVar )
                {
                    state.rendercount++;
                    if ( state.verbose == true )
                    {
                        printf( "Rendering %s [%ld]\n",
                                state.varname,
                                state.rendercount );
                    }

                    /* get the blob data */
                    VAR_Get( state.hVarServer, hVar, &obj );

                    /* print the blob */
                    PrintBlobObj( &state, &obj, fd );
                }

                /* Close the print session */
                VAR_ClosePrintSession( state.hVarServer,
                                       sigval,
                                       fd );
            }
            else if ( sig == SIG_VAR_CALC )
            {
                state.calccount++;
                if ( state.verbose == true )
                {
                    printf( "Calculating %s [%ld]\n",
                            state.varname,
                            state.calccount );
                }

                hVar = (VAR_HANDLE)sigval;
                GetRandomData( &state, &obj );
                VAR_Set( state.hVarServer, hVar, &obj );
            }
            else if ( sig == SIG_VAR_MODIFIED )
            {
                state.modifiedcount++;
                hVar = (VAR_HANDLE)sigval;
                VAR_Get( state.hVarServer, hVar, &obj );

                if ( state.quiet == false )
                {
                    dprintf( STDOUT_FILENO, "\033[H\033[J" );
                    if ( state.verbose == true )
                    {
                        dprintf( STDOUT_FILENO, "%s\n", state.varname );
                    }

                    PrintBlobObj( &state, &obj, STDOUT_FILENO );
                    printf("\n%ld\n", state.modifiedcount);
                }
            }
        }

        /* close the variable server */
        VARSERVER_Close( state.hVarServer );
    }
}

/*============================================================================*/
/*  usage                                                                     */
/*!
    Display the application usage

    The usage function dumps the application usage message to stderr.

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
                "usage: %s [-v] [-h] [-g] [-s] [-c] [-p] [-m] "
                " [-n count]"
                " [-d time] "
                " <blobname>\n"
                " [-v] : verbose mode\n"
                " [-h] : display this help\n"
                " [-g] : get the blob\n"
                " [-s] : set the blob\n"
                " [-c] : calculate the blob\n"
                " [-p] : print the blob\n"
                " [-w] : wait for modified blob\n"
                " [-n] : number of times to get or set (use with -s, -g)\n"
                " [-d] : delay (ms).  (use with -n, -s, -g)\n"
                " [-q] : quiet mode\n",
                cmdname );
    }
}

/*============================================================================*/
/*  ProcessOptions                                                            */
/*!
    Process the command line options

    The ProcessOptions function processes the command line options and
    populates the BlobTestState object

    @param[in]
        argC
            number of arguments
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @param[in]
        pState
            pointer to the BlobTest state object

    @return none

==============================================================================*/
static int ProcessOptions( int argC, char *argV[], BlobTestState *pState )
{
    int c;
    int result = EINVAL;
    const char *options = "vhgscpwn:d:q";

    if( ( pState != NULL ) &&
        ( argV != NULL ) )
    {
        while( ( c = getopt( argC, argV, options ) ) != -1 )
        {
            switch( c )
            {
                case 'h':
                    usage( argV[0] );
                    break;

                case 'g':
                    pState->get = true;
                    break;

                case 's':
                    pState->set = true;
                    break;

                case 'c':
                    pState->calc = true;
                    break;

                case 'p':
                    pState->print = true;
                    break;

                case 'w':
                    pState->wait = true;
                    break;

                case 'v':
                    pState->verbose = true;
                    break;

                case 'n':
                    pState->n = atol( optarg );
                    break;

                case 'd':
                    pState->delay = atoi( optarg );
                    break;

                case 'q':
                    pState->quiet = true;
                    break;

                default:
                    break;
            }
        }

        if ( optind < argC )
        {
            pState->varname = argV[optind];
        }
    }

    return 0;
}

/*============================================================================*/
/*  GetRandomData                                                             */
/*!
    Fill the var object with random data

    The GetRandomData function gets random data from /dev/urandom

    @param[in]
        pState
            pointer to the BlobTestState object

    @param[in]
       pVarObject
            pointer to the VarObject to populate

    @retval EOK - the object was populated successfully
    @retval ENOTSUP - object is not a blob type
    @retval EINVAL - invalid arguments

==============================================================================*/
static int GetRandomData( BlobTestState *pState, VarObject *pVarObject )
{
    int result = EINVAL;
    int fd;

    if ( ( pState != NULL ) &&
         ( pVarObject != NULL ) )
    {
        if ( pVarObject->type == VARTYPE_BLOB )
        {
            if ( pState->verbose == true )
            {
                printf( "Generating Random Blob Data for %s\n",
                        pState->varname );
            }

            fd = open("/dev/urandom", O_RDONLY );
            if ( fd != -1 )
            {
                read( fd, pVarObject->val.blob, pVarObject->len );
                close( fd );
                result = EOK;
            }
        }
        else
        {
            result = ENOTSUP;
        }
    }

    return result;
}

/*============================================================================*/
/*  PrintBlobObj                                                              */
/*!
    Print the content of the blob object

    The PrintBlobObj function prints the content of the blob object
    as a memory dump.

    @param[in]
        pState
            pointer to the BlobTestState object

    @param[in]
        pVarObject
            pointer to the blob variable to print

    @param[in]
        fd
            output file descriptor

    @retval EOK - blob printed successfully
    @retval ENOTSUP - variable is not a blob
    @retval ENOENT - no blob data
    @retval EINVAL - invalid arguments

==============================================================================*/
static int PrintBlobObj( BlobTestState *pState, VarObject *pObj, int fd )
{
    int result = EINVAL;
    uint8_t *p;
    int i;
    size_t len;

    if ( pObj != NULL )
    {
        if ( pObj->type == VARTYPE_BLOB )
        {
            len = pObj->len;
            p = (uint8_t *)(pObj->val.blob);
            if ( p != NULL )
            {
                for ( i = 0; i < len ; i ++ )
                {
                    if ( i % 32 == 0 )
                    {
                        dprintf(fd, "\n");
                    }

                    dprintf( fd, "%02X", p[i]);
                }

                result = EOK;
            }
            else
            {
                result = ENOENT;
            }
        }
        else
        {
            result = ENOTSUP;
        }
    }

    return result;
}

/*============================================================================*/
/*  SetupTerminationHandler                                                   */
/*!
    Set up an abnormal termination handler

    The SetupTerminationHandler function registers a termination handler
    function with the kernel in case of an abnormal termination of this
    process.

==============================================================================*/
static void SetupTerminationHandler( void )
{
    static struct sigaction sigact;

    memset( &sigact, 0, sizeof(sigact) );

    sigact.sa_sigaction = TerminationHandler;
    sigact.sa_flags = SA_SIGINFO;

    sigaction( SIGTERM, &sigact, NULL );
    sigaction( SIGINT, &sigact, NULL );

}

/*============================================================================*/
/*  TerminationHandler                                                        */
/*!
    Abnormal termination handler

    The TerminationHandler function will be invoked in case of an abnormal
    termination of this process.  The termination handler closes
    the connection with the variable server.

@param[in]
    signum
        The signal which caused the abnormal termination (unused)

@param[in]
    info
        pointer to a siginfo_t object (unused)

@param[in]
    ptr
        signal context information (ucontext_t) (unused)

--============================================================================*/
static void TerminationHandler( int signum, siginfo_t *info, void *ptr )
{
    syslog( LOG_ERR, "Abnormal termination of blobtest\n" );
    VARSERVER_Close( state.hVarServer );
    exit( 1 );
}

/*! @}
 * end of blobtest group */
