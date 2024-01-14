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
    handling and CALC, PRINT, MODIFIED, and QUEUE_MODIFIED signal handling
    for blob objects.

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
#include <time.h>
#include <inttypes.h>
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

    /*! use signalfd */
    bool use_signalfd;

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

    /*! client waits for notification queue */
    bool queuewait;

    /*! quiet mode - suppress all output */
    bool quiet;

    /*! name of blob variable */
    char *varname;

    /*! number of times to get or set */
    size_t n;

    /* delay in milliseconds between sets/gets */
    int delay;

    /*! count render actions*/
    uint32_t rendercount;

    /*! count calc actions */
    uint32_t calccount;

    /*! modified count */
    uint32_t modifiedcount;

    /*! object used for setting and getting the blob data */
    VarObject obj;

    /*! VarNotification object used for receiving queued notifications */
    VarNotification notification;

    /*! handle to the blob variable */
    VAR_HANDLE hTestVar;

} BlobTestState;

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! BlobTest State object */
BlobTestState state;

/*! timer */
timer_t timerID;

/*==============================================================================
        Private function declarations
==============================================================================*/

int main(int argc, char **argv);
static void usage( char *cmdname );
static int ProcessOptions( int argC, char *argV[], BlobTestState *pState );
static int GetRandomData( BlobTestState *pState, VarObject *pVarObject );
static int PrintBlobObj( VarObject *pObj, int fd );
static void SetupTerminationHandler( void );
static void TerminationHandler( int signum, siginfo_t *info, void *ptr );
static void CreateTimer( int timeoutms );
static int Setup( BlobTestState *pState );
static int SetupVarObject( BlobTestState *pState, size_t len );
static int SetupNotificationQueue( BlobTestState *pState, size_t len );
static int SetupTimer( BlobTestState *pState );
static int SetupNotifications( BlobTestState *pState );

static void Cleanup( BlobTestState *pState );
static int RunHandlers( BlobTestState *pState );
static int RunSignalfdHandlers( BlobTestState *pState );

static void HandlePrintRequest( BlobTestState *pState, int sigval );
static void HandleCalcRequest( BlobTestState *pState, int sigval );
static void HandleModifiedNotification( BlobTestState *pState, int sigval );
static void HandleQueueModifiedNotification( BlobTestState *pState );
static void HandleTimer( BlobTestState *pState );

/*==============================================================================
        Private function definitions
==============================================================================*/

/*============================================================================*/
/*  main                                                                      */
/*!
    Main entry point for the blobtest application

    The main function starts the blobtest application

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
    int rc;

    /* clear the state object */
    memset( &state, 0, sizeof( BlobTestState ) );

    /* set up some default state values */
    state.varname = "/sys/test/blob";
    state.n = 1;
    state.quiet = false;

    /* process the command line options */
    ProcessOptions( argc, argv, &state );

    if( ( argc < 2 ) ||
        ( state.usage == true ) )
    {
        usage( argv[0] );
        exit( 1 );
    }

    /* set up the state to handle VarServer interactions */
    rc = Setup( &state );
    if ( rc == EOK )
    {
        /* set up an abnormal termination handler */
        SetupTerminationHandler();

        /* Handle signals from the variable server */
        if ( state.use_signalfd )
        {
            /* use signalfd to get signals */
            RunSignalfdHandlers( &state );
        }
        else
        {
            /* use sigwaitinfo to get signals */
            RunHandlers( &state );
        }
    }
    else
    {
        printf("BLOBTEST: Failed (%d) %s\n", rc, strerror(rc));
    }

    /* close the variable server */
    VARSERVER_Close( state.hVarServer );

    /* clean up client resources */
    Cleanup( &state );

    return 0;
}

/*============================================================================*/
/*  RunHandlers                                                               */
/*!
    Handle VarServer signals

    The RunHandlers function waits for signals from the VarServer and
    invokes the appropriate handler function.

    The signals and their handlers are as follows:

    SIG_VAR_PRINT : HandlePrintRequest
    SIG_VAR_CALC : HandleCalcRequest
    SIG_VAR_MODIFIED : HandleModifiedNotification
    SIG_VAR_QUEUE_MODIFIED : HandleQueueModifiedNotification
    SIG_VAR_TIMER : HandleTimer

    @param[in]
       pState
            pointer to the BlobTestState object

    @retval EOK - handler execution completed
    @retval EINVAL - invalid arguments

==============================================================================*/
static int RunHandlers( BlobTestState *pState )
{
    int result = EINVAL;
    sigset_t mask;
    int sigval;
    int sig;
    siginfo_t info;

    if ( pState != NULL )
    {
        /* set up the VarServer signal mask to receive notifications */
        mask = VARSERVER_SigMask();

        /* loop until pState->n reaches zero */
        while( pState->n )
        {
            /* wait for a signal */
            sig = sigwaitinfo( &mask, &info );
            sigval = info.si_value.sival_int;
            if( sig == SIG_VAR_PRINT )
            {
                HandlePrintRequest( pState, sigval );
            }
            else if ( sig == SIG_VAR_CALC )
            {
                HandleCalcRequest( pState, sigval );
            }
            else if ( sig == SIG_VAR_MODIFIED )
            {
                HandleModifiedNotification( pState, sigval );
            }
            else if ( sig == SIG_VAR_QUEUE_MODIFIED )
            {
                HandleQueueModifiedNotification( pState );
            }
            else if ( sig == SIG_VAR_TIMER )
            {
                HandleTimer( pState );
            }
        }

        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  RunHandlers                                                               */
/*!
    Handle VarServer signals

    The RunHandlers function waits for signals from the VarServer and
    invokes the appropriate handler function.

    The signals and their handlers are as follows:

    SIG_VAR_PRINT : HandlePrintRequest
    SIG_VAR_CALC : HandleCalcRequest
    SIG_VAR_MODIFIED : HandleModifiedNotification
    SIG_VAR_QUEUE_MODIFIED : HandleQueueModifiedNotification
    SIG_VAR_TIMER : HandleTimer

    @param[in]
       pState
            pointer to the BlobTestState object

    @retval EOK - handler execution completed
    @retval EINVAL - invalid arguments

==============================================================================*/
static int RunSignalfdHandlers( BlobTestState *pState )
{
    int result = EINVAL;
    int sigval;
    int sig;
    int fd;

    if ( pState != NULL )
    {
        /* set up the signal file descriptor to receive notifications */
        fd = VARSERVER_Signalfd( 0 );

        /* loop until pState->n reaches zero */
        while( pState->n )
        {
            /* wait for a signal */
            sig = VARSERVER_WaitSignalfd( fd, &sigval );
            if( sig == SIG_VAR_PRINT )
            {
                HandlePrintRequest( pState, sigval );
            }
            else if ( sig == SIG_VAR_CALC )
            {
                HandleCalcRequest( pState, sigval );
            }
            else if ( sig == SIG_VAR_MODIFIED )
            {
                HandleModifiedNotification( pState, sigval );
            }
            else if ( sig == SIG_VAR_QUEUE_MODIFIED )
            {
                HandleQueueModifiedNotification( pState );
            }
            else if ( sig == SIG_VAR_TIMER )
            {
                HandleTimer( pState );
            }
        }

        result = EOK;
    }

    return result;
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
                " [-W] : wait for modified blob using queue notifcation\n"
                " [-n] : number of times to get or set (use with -s, -g)\n"
                " [-d] : delay (ms).  (use with -n, -s, -g)\n"
                " [-f] : use signalfd for varserver notifications\n"
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
    const char *options = "vhgscpwn:d:qWf";

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

                case 'W':
                    pState->queuewait = true;
                    break;

                case 'f':
                    pState->use_signalfd = true;
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
/*  Setup                                                                     */
/*!
    Set up the Blob Test Client

    The Setup function performs the following actions:

    - opens a handle to the Variable Server
    - gets a handle to the blob test variable
    - gets the blob test variable length
    - allocate storage for the blob test VarObject
    - set up the notification queue and allocate memory for notifications
    - set up a timer for periodic get/set actions
    - set up VarServer notifications

    @param[in]
        pState
            pointer to the BlobTestState object containing the timer delay

    @param[in]
        len
            size of the blob data

    @retval EOK Notification queue set up successfully
    @retval EINVAL invalid arguments
    @retval ENOMEM memory allocation failure

==============================================================================*/
static int Setup( BlobTestState *pState )
{
    int result = EINVAL;
    size_t len;

    if ( pState != NULL )
    {
        /* get a handle to the VAR server */
        pState->hVarServer = VARSERVER_Open();
        if( pState->hVarServer != NULL )
        {
            /* get a handle to the blob test variable */
            pState->hTestVar = VAR_FindByName( pState->hVarServer,
                                               pState->varname );
            if ( pState->hTestVar != VAR_INVALID )
            {
                /* get the length of the blob test variable */
                VAR_GetLength( pState->hVarServer, pState->hTestVar, &len );

                /* set up blob object storage */
                result = SetupVarObject( pState, len );
                if ( result == EOK )
                {
                    /* set up blob notification queue */
                    result = SetupNotificationQueue( pState, len );
                    if ( result == EOK )
                    {
                        /* set up timer */
                        result = SetupTimer( pState );
                        if ( result == EOK )
                        {
                            /* set up VarServer notifications */
                            result = SetupNotifications( pState );
                        }
                    }
                }
            }
            else
            {
                /* unable to find blob test variable */
                result = ENOENT;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  SetupVarObject                                                            */
/*!
    Set up the VarObject memory

    The SetupVarObject function allocates memory to store the received
    blob data, and sets up the VarObject type and length fields.

    @param[in]
        pState
            pointer to the BlobTestState object containing the timer delay

    @param[in]
        len
            size of the blob data

    @retval EOK Notification queue set up successfully
    @retval EINVAL invalid arguments
    @retval ENOMEM memory allocation failure

==============================================================================*/
static int SetupVarObject( BlobTestState *pState, size_t len )
{
    int result = EINVAL;

    if ( ( pState != NULL ) &&
         ( len > 0 ) )
    {
        /* clear the var object */
        memset( &pState->obj, 0, sizeof( VarObject ) );

        /* set up the VarObject */
        pState->obj.type = VARTYPE_BLOB;
        pState->obj.len = len;

        /* allocate memory to store the received blob */
        pState->obj.val.blob = malloc( len );
        if ( pState->obj.val.blob == NULL )
        {
            /* unable to allocate memory for the blob object */
            result = ENOMEM;
        }
        else
        {
            result = EOK;
        }
    }

    return result;
}

/*============================================================================*/
/*  SetupNotificationQueue                                                                */
/*!
    Set up the client notification queue

    The SetupNotificationQueue function sets up notification queue used
    for receiving blob data objects from VarServer when they change.
    The size of the queue messages must be big enough to contain a
    VarNotification object and the blob data itself.  This function also
    allocates memory to store the received blob object data.

    @param[in]
        pState
            pointer to the BlobTestState object containing the timer delay

    @param[in]
        len
            size of the blob data

    @retval EOK Notification queue set up successfully
    @retval EINVAL invalid arguments
    @retval ENOMEM memory allocation failure
    @retval other error from VARSERVER_CreateClientQueue

==============================================================================*/
static int SetupNotificationQueue( BlobTestState *pState, size_t len )
{
    int result = EINVAL;

    if ( ( pState != NULL ) &&
         ( len > 0 ) )
    {
        if ( pState->queuewait == true )
        {
            /* initialize the notification object */
            memset( &pState->notification, 0, sizeof(VarNotification) );
            pState->notification.obj.len = len;
            pState->notification.obj.type = VARTYPE_BLOB;

            /* allocate memory for the received blob */
            pState->notification.obj.val.blob = malloc(len);
            if ( pState->notification.obj.val.blob == NULL )
            {
                result = ENOMEM;
            }
            else
            {
                /* create a message notification queue for this client.
                set the message size based on the size of the blob */
                len += sizeof( VarNotification );
                result = VARSERVER_CreateClientQueue( pState->hVarServer,
                                                      10,
                                                      len );
            }
        }
        else
        {
            /* nothing to do */
            result = EOK;
        }
    }

    return result;
}

/*============================================================================*/
/*  SetupTimer                                                                */
/*!
    Set up Timer notification

    The SetupTimer function sets up a repeating timer with a delay
    specified in milliseconds (min interval = 1ms).  This timer is
    used to trigger blob get/set actions.

    @param[in]
        pState
            pointer to the BlobTestState object containing the timer delay

==============================================================================*/
static int SetupTimer( BlobTestState *pState )
{
    int result = EINVAL;

    if ( pState != NULL )
    {
        /* setup timer */
        if ( pState->delay > 0 )
        {
            CreateTimer( pState->delay );
        }

        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  SetupNotifications                                                        */
/*!
    Set up VarServer notifications

    The SetupNotifications requests notifications from VarServer for the
    following notification types if they are enabled:

    - NOTIFY_PRINT - client will render the blob data for another client
    - NOTIFY_CALC - client will generate new blob data
    - NOTIFY_MODIFIED - client will be notified when new blob data is available
    - NOTIFY_MODIFIED_QUEUE - client will be notified when new blob data has
                              been delivered to its message queue.

    @param[in]
        pState
            pointer to the BlobTestState object

==============================================================================*/
static int SetupNotifications( BlobTestState *pState )
{
    int result = EINVAL;
    int rc;

    if ( pState != NULL )
    {
        /* assume all is ok until it is not */
        result = EOK;

        if ( pState->print == true )
        {
            /* request a print noticication from VarServer */
            rc = VAR_Notify( pState->hVarServer,
                             pState->hTestVar,
                             NOTIFY_PRINT );
            if ( rc != EOK )
            {
                result = rc;
            }
        }

        if ( pState->calc == true )
        {
            /* request a calc notification from VarServer */
            rc = VAR_Notify( pState->hVarServer,
                             pState->hTestVar,
                             NOTIFY_CALC );
            if ( rc != EOK )
            {
                result = rc;
            }
        }

        if ( state.wait == true )
        {
            /* request a modified notification from VarServer */
            rc = VAR_Notify( pState->hVarServer,
                             pState->hTestVar,
                             NOTIFY_MODIFIED );
            if ( rc != EOK )
            {
                result = rc;
            }
        }

        if ( state.queuewait == true )
        {
            /* request a queue modified notification from VarServer */
            rc = VAR_Notify( pState->hVarServer,
                             pState->hTestVar,
                             NOTIFY_MODIFIED_QUEUE );
            if ( rc != EOK )
            {
                result = rc;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  Cleanup                                                                   */
/*!
    Clean up local resources

    The Cleanup function deallocates local resources used by the blob test
    client.  This includes memory used to store recevied blobs, and
    var notifications.

    @param[in]
        pState
            pointer to the BlobTestState object

==============================================================================*/
static void Cleanup( BlobTestState *pState )
{
    if ( pState != NULL )
    {
        if ( pState->notification.obj.val.blob != NULL )
        {
            free( pState->notification.obj.val.blob );
            pState->notification.obj.val.blob = NULL;
        }

        if ( pState->obj.val.blob != NULL )
        {
            free( pState->obj.val.blob );
            pState->obj.val.blob = NULL;
        }
    }
}

/*============================================================================*/
/*  HandlePrintRequest                                                        */
/*!
    Handle a PRINT request from VarServer

    The HandlePrintRequest function creates a new Print Session with
    VarServer using the VAR_OpenPrintSession function.  This creates a
    file descriptor which can write to the requesting client's output
    stream.  It then gets the latest value of the blob, and renders it
    using the PrintBlobObj function to the requesting clients output
    stream.  It then closes the output stream using the VAR_ClosePrintSession
    function which unblocks the requesting client.

    @param[in]
        pState
            pointer to the BlobTestState object

    @param[in]
        sigval
            signal value containing the handle of the blob variable

==============================================================================*/
static void HandlePrintRequest( BlobTestState *pState, int sigval )
{
    VAR_HANDLE hVar;
    int fd;

    if ( pState != NULL )
    {
        /* open a print session */
        VAR_OpenPrintSession( pState->hVarServer,
                              sigval,
                              &hVar,
                              &fd );

        if ( hVar == pState->hTestVar )
        {
            state.rendercount++;
            if ( pState->verbose == true )
            {
                printf( "Rendering %s [%"PRIu32"]\n",
                        pState->varname,
                        pState->rendercount );
            }

            /* get the blob data */
            VAR_Get( pState->hVarServer, hVar, &pState->obj );

            /* print the blob */
            PrintBlobObj( &pState->obj, fd );
        }

        /* Close the print session */
        VAR_ClosePrintSession( pState->hVarServer,
                               sigval,
                               fd );

    }
}

/*============================================================================*/
/*  HandleCalcRequest                                                         */
/*!
    Handle a CALC request from VarServer

    The HandleCalcRequest generates some random blob data and
    stores it to VarServer using the VAR_Set function.

    @param[in]
        pState
            pointer to the BlobTestState object

    @param[in]
        sigval
            signal value containing the handle of the blob variable

==============================================================================*/
static void HandleCalcRequest( BlobTestState *pState, int sigval )
{
    VAR_HANDLE hVar = (VAR_HANDLE)sigval;

    if ( pState != NULL )
    {
        if ( pState->hTestVar == hVar )
        {
            pState->calccount++;
            if ( pState->verbose == true )
            {
                printf( "Calculating %s [%"PRIu32"]\n",
                        pState->varname,
                        pState->calccount );
            }

            GetRandomData( pState, &pState->obj );
            VAR_Set( pState->hVarServer, hVar, &pState->obj );
        }
    }
}

/*============================================================================*/
/*  HandleModifiedNotification                                                */
/*!
    Handle a modified notification from VarServer

    The HandleModifiedNotification function requests the value of the
    modified blob data from the VarServer and prints it if verbose
    output is enabled.

    @param[in]
        pState
            pointer to the BlobTestState object

==============================================================================*/
static void HandleModifiedNotification( BlobTestState *pState, int sigval )
{
    VAR_HANDLE hVar;
    int rc;

    if ( pState != NULL )
    {
        pState->modifiedcount++;
        hVar = (VAR_HANDLE)sigval;
        rc = VAR_Get( pState->hVarServer, hVar, &pState->obj );
        if ( rc == EOK )
        {
            if ( pState->quiet == false )
            {
                dprintf( STDOUT_FILENO, "\033[H\033[J" );
                if ( pState->verbose == true )
                {
                    dprintf( STDOUT_FILENO, "%s\n", pState->varname );
                }

                PrintBlobObj( &pState->obj, STDOUT_FILENO );
                printf("\n%"PRIu32"\n", pState->modifiedcount);
            }
        }
    }
}

/*============================================================================*/
/*  HandleQueueModifiedNotification                                           */
/*!
    Handle a queue modified notification from VarServer

    The HandleQueueModifiedNotification function invokes the VAR_GetFromQueue
    function to pull VarNotifications from the client's message queue.
    This does not involve interation with VarServer, only with the local
    client's queue.

    Any received blob data is printed if verbose output is enabled

    @param[in]
        pState
            pointer to the BlobTestState object

==============================================================================*/
static void HandleQueueModifiedNotification( BlobTestState *pState )
{
    char *buf = NULL;
    size_t len = 0;
    int rc;

    if ( pState != NULL )
    {
        /* use the VarServer client working buffer for message receiption */
        /* this should be bigger than the largest expected message */
        VARSERVER_GetWorkingBuffer( pState->hVarServer, &buf, &len);

        do
        {
            /* get the data from the queue (non-blocking) */
            rc = VAR_GetFromQueue( pState->hVarServer,
                                   &pState->notification,
                                   buf,
                                   len );
            if ( rc == EOK )
            {
                pState->modifiedcount++;

                if ( pState->quiet == false )
                {
                    dprintf( STDOUT_FILENO, "\033[H\033[J" );
                    if ( pState->verbose == true )
                    {
                        dprintf( STDOUT_FILENO, "%s\n", state.varname );
                    }

                    PrintBlobObj( &pState->notification.obj,
                                  STDOUT_FILENO );

                    printf("\n%"PRIu32"\n", pState->modifiedcount);
                }
            }
        } while ( rc == EOK );
    }
}

/*============================================================================*/
/*  HandleTimer                                                               */
/*!
    Handle Timer expiry

    The HandleTimer function performs actions when the timer expires.
    Actions performed are:

    - set random blob data to VarServer if the SET action is enabled, OR
    - get blob data from VarServer if the GET action is enabled

    @param[in]
        pState
            pointer to the BlobTestState object

==============================================================================*/
static void HandleTimer( BlobTestState *pState )
{
    int rc;

    if ( pState != NULL )
    {
        if ( pState->set == true )
        {
            if ( pState->verbose == true )
            {
                printf("Setting %s\n", pState->varname );
            }

            GetRandomData( pState, &pState->obj );
            VAR_Set( pState->hVarServer, pState->hTestVar, &pState->obj );
            pState->n--;
        }
        else if ( pState->get == true )
        {
            if ( pState->verbose == true )
            {
                printf("Getting %s\n", pState->varname );
            }

            rc = VAR_Get( pState->hVarServer, pState->hTestVar, &pState->obj );
            if ( rc == EOK )
            {
                pState->n--;

                if ( pState->verbose == true )
                {
                    PrintBlobObj( &pState->obj, STDOUT_FILENO );
                    printf("\n");
                }
            }
        }
    }
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
    ssize_t n;

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
                n = read( fd, pVarObject->val.blob, pVarObject->len );
                if ( n == (ssize_t)(pVarObject->len) )
                {
                    result = EOK;
                }

                close( fd );

                if ( pState->calc == true )
                {
                    memcpy( pVarObject->val.blob,
                            &pState->calccount,
                            sizeof( size_t ));
                }
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
static int PrintBlobObj( VarObject *pObj, int fd )
{
    int result = EINVAL;
    uint8_t *p;
    size_t i;
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
    /* signum, ptr, and info are unused */
    (void)signum;
    (void)info;
    (void)ptr;

    syslog( LOG_ERR, "Abnormal termination of blobtest\n" );
    VARSERVER_Close( state.hVarServer );

    Cleanup( &state );

    exit( 1 );
}

/*============================================================================*/
/*  CreateTimer                                                               */
/*!
    Create a repeating timer

    The CreateTimer function creates a timer used for periodic test
    execution.

@param[in]
    timeoutms
        timer interval in milliseconds

==============================================================================*/
static void CreateTimer( int timeoutms )
{
    struct sigevent te;
    struct itimerspec its;
    int sigNo = SIG_VAR_TIMER;
    long secs;
    long msecs;

    secs = timeoutms / 1000;
    msecs = timeoutms % 1000;

    /* Set and enable alarm */
    te.sigev_notify = SIGEV_SIGNAL;
    te.sigev_signo = sigNo;
    te.sigev_value.sival_int = 1;
    timer_create(CLOCK_REALTIME, &te, &timerID);

    its.it_interval.tv_sec = secs;
    its.it_interval.tv_nsec = msecs * 1000000L;
    its.it_value.tv_sec = secs;
    its.it_value.tv_nsec = msecs * 1000000L;
    timer_settime(timerID, 0, &its, NULL);
}

/*! @}
 * end of blobtest group */
