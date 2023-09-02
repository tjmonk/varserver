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
 * @defgroup handlers variable server handlers
 * @brief Handlers functions for client requests
 * @{
 */

/*============================================================================*/
/*!
@file handlers.c

    Variable Server Client Handlers

    The Variable Server Client Handlers handle client requests.

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syslog.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include <varserver/varclient.h>
#include <varserver/varserver.h>
#include "varlist.h"
#include "taglist.h"
#include "blocklist.h"
#include "transaction.h"
#include "stats.h"
#include "metric.h"
#include "handlers.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*==============================================================================
        Private types
==============================================================================*/

/*! the RequestHandler object defines a request handler */
typedef struct _RequestHandler
{
    /*! the type of request this handler is for */
    VarRequest requestType;

    /*! request name */
    char *requestName;

    /*! pointer to the request handler function */
    int (*handler)( VarClient *pVarClient );

    /*! pointer to the name of the metric counter */
    char *pMetricName;

    /*! counter for the number of times this request has been made */
    uint64_t *pMetric;

} RequestHandler;

/*==============================================================================
        Private function declarations
==============================================================================*/

/*! The RenderHandler object maps a Var Server variable handle to a
    function which will render its output */
typedef struct renderHandler
{
    /* handle to the variable to be rendered */
    VAR_HANDLE hVar;

    /*! pointer to the render handling function */
    int (*fn)(VarInfo *pVarInfo, char *buf, size_t len);

    /*! pointer to the next render handler in the list */
    struct renderHandler *pNext;
} RenderHandler;

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! pointer to a list of render handlers */
static RenderHandler *pRenderHandlers = NULL;

/*! Request Handlers - these must appear in the exact same order
    as the request enumerations so they can be looked up directly
    in the Request array */
static RequestHandler RequestHandlers[] =
{
    {
        VARREQUEST_INVALID,
        "IMVALID",
        ProcessVarRequestInvalid,
        NULL,
        NULL
    },
    {
        VARREQUEST_OPEN,
        "OPEN",
        ProcessVarRequestOpen,
        NULL,
        NULL
    },
    {
        VARREQUEST_CLOSE,
        "CLOSE",
        ProcessVarRequestClose,
        "/varserver/stats/close",
        NULL
    },
    {
        VARREQUEST_ECHO,
        "ECHO",
        ProcessVarRequestEcho,
        "/varserver/stats/echo",
        NULL
    },
    {
        VARREQUEST_NEW,
        "NEW",
        ProcessVarRequestNew,
        "/varserver/stats/new",
        NULL
    },
    {
        VARREQUEST_FIND,
        "FIND",
        ProcessVarRequestFind,
        "/varserver/stats/find",
        NULL
    },
    {
        VARREQUEST_GET,
        "GET",
        ProcessVarRequestGet,
        "/varserver/stats/get",
        NULL
    },
    {
        VARREQUEST_PRINT,
        "PRINT",
        ProcessVarRequestPrint,
        "/varserver/stats/print",
        NULL
    },
    {
        VARREQUEST_SET,
        "SET",
        ProcessVarRequestSet,
        "/varserver/stats/set",
        NULL
    },
    {
        VARREQUEST_TYPE,
        "TYPE",
        ProcessVarRequestType,
        "/varserver/stats/type",
        NULL
    },
    {
        VARREQUEST_NAME,
        "NAME",
        ProcessVarRequestName,
        "/varserver/stats/name",
        NULL
    },
    {
        VARREQUEST_LENGTH,
        "LENGTH",
        ProcessVarRequestLength,
        "/varserver/stats/length",
        NULL
    },
    {
        VARREQUEST_NOTIFY,
        "NOTIFY",
        ProcessVarRequestNotify,
        "/varserver/stats/notify",
        NULL
    },
    {
        VARREQUEST_GET_VALIDATION_REQUEST,
        "VALIDATION_REQUEST",
        ProcessValidationRequest,
        "/varserver/stats/validate_request",
        NULL
    },
    {
        VARREQUEST_SEND_VALIDATION_RESPONSE,
        "VALIDATION_RESPONSE",
        ProcessValidationResponse,
        "/varserver/stats/validation_response",
        NULL
    },
    {
        VARREQUEST_OPEN_PRINT_SESSION,
        "OPEN_PRINT_SESSION",
        ProcessVarRequestOpenPrintSession,
        "/varserver/stats/open_print_session",
        NULL
    },
    {
        VARREQUEST_CLOSE_PRINT_SESSION,
        "CLOSE_PRINT_SESSION",
        ProcessVarRequestClosePrintSession,
        "/varserver/stats/close_print_session",
        NULL
    },
    {
        VARREQUEST_GET_FIRST,
        "GET_FIRST",
        ProcessVarRequestGetFirst,
        "/varserver/stats/get_first",
        NULL
    },
    {
        VARREQUEST_GET_NEXT,
        "GET_FIRST",
        ProcessVarRequestGetNext,
        "/varserver/stats/get_next",
        NULL
    }
};

/*==============================================================================
        Public function definitions
==============================================================================*/

/*============================================================================*/
/*  InitHandlerMetrics                                                        */
/*!

    Initiialize the metrics for the request handlers

    The InitHandlerMetrics function creates a metric for each
    handler function which will track the number of invocations of
    each request handler.

    @param[in]
        pInfo
            pointer to a siginfo_t object containing the request information

    @retval EOK the request was processed successfully

==============================================================================*/
int InitHandlerMetrics( void )
{
    int n;
    int i;
    char *pMetricName;
    int result;
    uint64_t *pMetric;

    /* make metrics for all the request endpoints */
    n = sizeof(RequestHandlers) / sizeof(RequestHandler);
    for ( i=0; i<n; i++ )
    {
        pMetricName = RequestHandlers[i].pMetricName;
        if( pMetricName != NULL )
        {
            pMetric = MakeMetric( pMetricName );
            RequestHandlers[i].pMetric = pMetric;
        }
    }

    result = EOK;
}

/*============================================================================*/
/*  Handle Request                                                            */
/*!

    Handle a request from a client

    The HandleRequest function handles requests received from clients
    Requests include:
        - closing the interface between the client and the server

    @param[in]
        pInfo
            pointer to a siginfo_t object containing the request information

    @retval EOK the request was processed successfully
    @retval EINPROGRESS the request is pending and the client will remain
            blocked until the request is complete
    @retval EINVAL invalid argument

==============================================================================*/
int HandleRequest( VarClient *pVarClient )
{
    int result = EINVAL;
    VarRequest requestType;
    int (*handler)(VarClient *pVarClient);
    uint64_t *pMetric;

    if ( pVarClient != NULL )
    {
        /* update the request stats */
        STATS_IncrementRequestCount();

        if( pVarClient->rr.requestType >= VARREQUEST_END_MARKER )
        {
            requestType = VARREQUEST_INVALID;
        }
        else
        {
            requestType = pVarClient->rr.requestType;
        }

        if( pVarClient->debug >= LOG_DEBUG )
        {
            printf("SERVER: Processing request %s from client %d\n",
                    RequestHandlers[requestType].requestName,
                    pVarClient->rr.clientid);
        }

        /* update the metric */
        pMetric = RequestHandlers[requestType].pMetric;
        if ( pMetric != NULL )
        {
            (*pMetric)++;
        }

        /* increment the client's transaction counter */
        (pVarClient->transactionCount)++;

        /* get the appropriate handler */
        handler = RequestHandlers[requestType].handler;
        if( handler != NULL )
        {
            /* invoke the handler */
            result = handler( pVarClient );
        }
        else
        {
            /* this function is not supported yet */
            printf("requestType %d is not supported\n", requestType);
            result = ENOTSUP;
        }

        /* a result code of EINPROGRESS indicates the the variable server
            has passed the requested transaction off to another client
            unblocking this client request will be deferred until
            the other client responds */
        if( result != EINPROGRESS )
        {
            /* unblock the client so it can proceed */
            /* we don't unblock OPEN and CLOSE requests */
            if( ( requestType != VARREQUEST_CLOSE ) &&
                ( requestType != VARREQUEST_OPEN ) )
            {
                UnblockClient( pVarClient );
            }
        }
    }
    else
    {
        printf("SERVER: Invalid var client : NULL pointer\n");
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestOpen                                                     */
/*!
    Open a client connection

    Handle a request from a client to open its connection to the
    server.  The client open function (if specified) is called
    and the client is unblocked

    @param[in]
        pVarClient
            pointer to the variable client object

    @retval EOK the client was closed successfully
    @retval EINVAL invalid argument
    @retval errno error number returned by munmap

==============================================================================*/
int ProcessVarRequestOpen( VarClient *pVarClient )
{
    int result = EINVAL;
    int clientid = 0;

    if( pVarClient != NULL )
    {
        result = EOK;

        if( pVarClient->debug >= LOG_DEBUG )
        {
            printf("SERVER: Opening Client\n");
        }

        pVarClient->rr.responseVal = 0;

        if ( pVarClient->pFnOpen != NULL )
        {
            result = pVarClient->pFnOpen( pVarClient );
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestClose                                                    */
/*!
    Terminate a client connection

    Handle a request from a client to terminate its connection to the
    server.  The client is unblocked, the client memory map is closed,
    and the client reference is deleted.

    @param[in]
        pVarClient
            pointer to the variable client object

    @retval EOK the client was closed successfully
    @retval EINVAL invalid argument
    @retval errno error number returned by munmap

==============================================================================*/
int ProcessVarRequestClose( VarClient *pVarClient )
{
    int result = EINVAL;
    int clientid = 0;

    if( pVarClient != NULL )
    {
        result = EOK;

        if( pVarClient->debug >= LOG_DEBUG )
        {
            printf("SERVER: Closing Client\n");
        }

        pVarClient->rr.responseVal = 0;

        /* allow the client to proceed */
        UnblockClient( pVarClient );

        if ( pVarClient->pFnClose != NULL )
        {
            result = pVarClient->pFnClose( pVarClient );
        }
    }

    return result;
}

/*============================================================================*/
/*  UnblockClient                                                             */
/*!
    Unblock the client connection

    Unblocking the client connection allows the client to return from its
    API call to the server.

    @param[in]
        pVarClient
            pointer to the variable client object

    @retval EOK the client was closed successfully
    @retval EINVAL invalid argument

==============================================================================*/
int UnblockClient( VarClient *pVarClient )
{
    int result = EINVAL;

    if( pVarClient != NULL )
    {
        if ( pVarClient->pFnUnblock != NULL )
        {
            /* call the client specific unblocking function */
            result = pVarClient->pFnUnblock( pVarClient );
        }
        else
        {
            result = ENOTSUP;
        }
    }

    return result;
}

/*============================================================================*/
/*  ValidateClient                                                            */
/*!
    Validate a client reference

    The ValidateClient function checks that the specified pointer
    references a valid varclient for this server.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the client is valid
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ValidateClient( VarClient *pVarClient )
{
    int result = EINVAL;

    if( ( pVarClient != NULL ) &&
        ( pVarClient->rr.id == VARSERVER_ID ) )
    {
        if( pVarClient->rr.version == VARSERVER_VERSION )
        {
            result = EOK;
        }
        else
        {
            result = ENOTSUP;
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestNew                                                      */
/*!
    Process a NEW variable request from a client

    The ProcessVarRequestNew function handles a "NEW variable" request
    from a client.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the new variable was created
    @retval ENOMEM memory allocation failure
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestNew( VarClient *pVarClient )
{
    int result = EINVAL;
    uint32_t varhandle;
    int rc;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* special handling for string variables uses the client's
           working buffer to pass the variable value */
        if( pVarClient->rr.variableInfo.var.type == VARTYPE_STR )
        {
            pVarClient->rr.variableInfo.var.val.str = &pVarClient->workbuf;
        }

        /* special handling for blob variables uses the client's
           working buffer to pass the variable value */
        if( pVarClient->rr.variableInfo.var.type == VARTYPE_BLOB )
        {
            pVarClient->rr.variableInfo.var.val.blob = &pVarClient->workbuf;
        }

        /* add the new variable to the variable list */
        rc = VARLIST_AddNew( &pVarClient->rr.variableInfo, &varhandle );
        if( rc == EOK )
        {
            /* return the new variable's handle */
            pVarClient->rr.responseVal = varhandle;
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestFind                                                     */
/*!
    Process a FIND variable request from a client

    The ProcessVarRequestFind function handles a "FIND variable" request
    from a client.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the new variable was found
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestFind( VarClient *pVarClient )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* search for the variable in the variable list */
        result = VARLIST_Find( &pVarClient->rr.variableInfo,
                               (VAR_HANDLE *)&pVarClient->rr.responseVal );
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestEcho                                                     */
/*!
    Process an ECHO request from a client

    The ProcessVarRequestEcho function handles a test ECHO request
    from a client.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the request was successful
    @retval EINVAL the client is invalid

==============================================================================*/
int ProcessVarRequestEcho( VarClient *pVarClient )
{
    int result = EINVAL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* echo request back to response */
        pVarClient->rr.responseVal = pVarClient->rr.requestVal;
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestInvalid                                                  */
/*!
    Handler for invalid requests from a client

    The ProcessVarRequestInvalid function catches any unsupported/invalid
    request from a client.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval ENOTSUP the request is not supported
    @retval EINVAL the client is invalid

==============================================================================*/
int ProcessVarRequestInvalid( VarClient *pVarClient )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* tell the client this request is not supported */
        result = ENOTSUP;
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestPrint                                                    */
/*!
    Process a PRINT variable request from a client

    The ProcessVarRequestPrint function handles a "PRINT variable" request
    from a client.  It populates the variable value and the format specifier
    into the client's VarInfo object

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable info was returned
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestPrint( VarClient *pVarClient )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;
    pid_t handler;
    RenderHandler *pRenderHandler = pRenderHandlers;
    int (*fn)(VarInfo *pVarInfo, char *buf, size_t len);
    bool handled = false;
    VAR_HANDLE hVar;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        while( pRenderHandler != NULL )
        {
            if ( pRenderHandler->hVar == pVarClient->rr.variableInfo.hVar )
            {
                if ( pRenderHandler->fn != NULL )
                {
                    fn = pRenderHandler->fn;
                    result = fn( &pVarClient->rr.variableInfo,
                                 &pVarClient->workbuf,
                                 pVarClient->workbufsize );

                    handled = true;
                    break;
                }
            }

            /* select the next render handler */
            pRenderHandler = pRenderHandler->pNext;
        }

        if ( handled == false )
        {
            result = VARLIST_PrintByHandle( pVarClient->rr.clientid,
                                            &pVarClient->rr.variableInfo,
                                            &pVarClient->workbuf,
                                            pVarClient->workbufsize,
                                            &pVarClient->rr.len,
                                            pVarClient,
                                            &handler );
        }

        /* capture the result */
        pVarClient->rr.responseVal = result;

        if( result == EINPROGRESS )
        {
            /* another client needs to calculate the value before
               we can print it */
            /* add the client to the blocked clients list */
            hVar = pVarClient->rr.variableInfo.hVar;
            BlockClient( pVarClient, NOTIFY_CALC, hVar );
        }
        else if(  result == ESTRPIPE )
        {
            /* printing is being handled by another client */
            /* get the PID of the client handling the printing */
            pVarClient->rr.peer_pid = handler;

            /* don't unblock the requesting client */
            result = EINPROGRESS;
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestOpenPrintSession                                         */
/*!
    Handle a request to open a print session

    The ProcessVarRequestOpenPrintSession function handles a request
    from a client to open a print session.  It gets the variable handle
    for the variable being printed, and gets the VarClient object
    for the client which is requesting the variable to be printed.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable info was returned
    @retval ENOENT the print session was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestOpenPrintSession( VarClient *pVarClient )
{
    int result = EINVAL;
    VarClient *pRequestor;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* get the print session transaction */
        pRequestor = (VarClient *)TRANSACTION_Get( pVarClient->rr.requestVal );
        if( pRequestor != NULL )
        {
            /* get the handle for the variable being requested */
            pVarClient->rr.variableInfo.hVar = pRequestor->rr.variableInfo.hVar;

            /* get the PID of the client requesting the print */
            pVarClient->rr.peer_pid = pRequestor->rr.client_pid;

            /* get the PID of the client performing the print */
            pRequestor->rr.peer_pid = pVarClient->rr.client_pid;

            /* unblock the requesting client */
            UnblockClient( pRequestor );

            /* indicate success */
            result = EOK;
        }
        else
        {
            /* print session transaction was not found */
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestClosePrintSession                                        */
/*!
    Handle a request to close a print session

    The ProcessVarRequestClosePrintSession function handles a request
    from a client to close a print session.

    The client which is requesting the variable to be printed is unblocked
    by this function.

    @param[in]
        pVarClient
            Pointer to the client data structure of the client closing
            the print session.

    @retval EOK the print session was closed
    @retval ENOENT the print session was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestClosePrintSession( VarClient *pVarClient )
{
    int result = EINVAL;
    VarClient *pRequestor;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* get the client object of the requestor of the print */
        pRequestor = (VarClient *)TRANSACTION_Remove( pVarClient->rr.requestVal );
        if( pRequestor != NULL )
        {
            /* unblock the reqeusting client */
            UnblockClient( pRequestor );

            result = EOK;
        }
        else
        {
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestSet                                                      */
/*!
    Process a SET variable request from a client

    The ProcessVarRequestSet function handles a "SET variable" request
    from a client.  It populates the variable value into the varstorage
    for the variable

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable value was set
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestSet( VarClient *pVarClient )
{
    int result = EINVAL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* check if we are dealing with a string */
        if( pVarClient->rr.variableInfo.var.type == VARTYPE_STR )
        {
            /* for strings, the data is transferred via the working buffer */
            pVarClient->rr.variableInfo.var.val.str = &pVarClient->workbuf;
        }

        /* check if we are dealing with a blob */
        if( pVarClient->rr.variableInfo.var.type == VARTYPE_BLOB )
        {
            /* for strings, the data is transferred via the working buffer */
            pVarClient->rr.variableInfo.var.val.blob = &pVarClient->workbuf;
        }

        /* set the variable value */
        result = VARLIST_Set( pVarClient->rr.clientid,
                              &pVarClient->rr.variableInfo,
                              &pVarClient->validationInProgress,
                              (void *)pVarClient );

        pVarClient->rr.responseVal = result;
    }

    if( ( result != EOK ) &&
        ( pVarClient->debug >= LOG_DEBUG ) )
    {
        printf( "SERVER: %s result = %s\n", __func__, strerror( result ) );
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestType                                                     */
/*!
    Process a TYPE variable request from a client

    The ProcessVarRequestType function handles a "variable TYPE" request
    from a client.  It requests the variable type for the specified
    variable

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable type was retrieved
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestType( VarClient *pVarClient )
{
    int result = EINVAL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* fet the variable type */
        result = VARLIST_GetType( &pVarClient->rr.variableInfo );
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestName                                                     */
/*!
    Process a NAME variable request from a client

    The ProcessVarRequestName function handles a "variable NAME" request
    from a client.  It requests the variable name for the specified
    variable

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable type was retrieved
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestName( VarClient *pVarClient )
{
    int result = EINVAL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* fetch the variable name */
        result = VARLIST_GetName( &pVarClient->rr.variableInfo );
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestLength                                                   */
/*!
    Process a LENGTH variable request from a client

    The ProcessVarRequestLength function handles a "variable LENGTH" request
    from a client.  It requests the variable length for the specified
    variable.  This is mostly useful for variable length data types such
    as strings.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable length was retrieved
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestLength( VarClient *pVarClient )
{
    int result = EINVAL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* get the variable length */
        result = VARLIST_GetLength( &(pVarClient->rr.variableInfo) );
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestNotify                                                   */
/*!
    Process a NOTIFY variable request from a client

    The ProcessVarRequestNotify function handles a variable notification
    requests from a client.  It registers a request for a notification
    of type:
        NOTIFY_MODIFIED_QUEUE
            - request a notification when the notification queue changes
        NOTIFY_MODIFIED
            - request a notification when a variable's value changes
        NOTIFY_CALC
            - request a notification when a variable's value is requested
              by another client
        NOTIFY_VALIDATE
            - request a notification when a variable is being changed
              by another client, before the change is applied.
        NOTIFY_PRINT
            - request a notification when a client tries to print
              the variable with the given handle

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable notification was successfully registered
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestNotify( VarClient *pVarClient )
{
    int result = EINVAL;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        /* register the notification request */
        result = VARLIST_RequestNotify( pVarClient->rr.clientid,
                                        &(pVarClient->rr.variableInfo),
                                        pVarClient->rr.client_pid,
                                        -1 );
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestGet                                                      */
/*!
    Process a GET variable request from a client

    The ProcessVarRequestGet function handles a "GET variable" request
    from a client.  It populates the variable type, value and length
    into the client's VarInfo object.  If the variable is a string,
    the string is transferred into the client'sworking buffer.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable info was returned
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestGet( VarClient *pVarClient )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;
    VAR_HANDLE hVar;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        result = VARLIST_GetByHandle( pVarClient->rr.clientid,
                                      &pVarClient->rr.variableInfo,
                                      &pVarClient->workbuf,
                                      pVarClient->workbufsize,
                                      &pVarClient->rr.len );
        if( result == EINPROGRESS )
        {
            hVar = pVarClient->rr.variableInfo.hVar;

            /* add the client to the blocked clients list */
            BlockClient( pVarClient, NOTIFY_CALC, hVar );
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestGetFirst                                                 */
/*!
    Process a GET_FIRST variable request from a client

    The ProcessVarRequestGetFirst function handles a "GET_FIRST variable"
    request from a client.  It initiates a search for variables which
    match the specified search criteria.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable info was returned
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestGetFirst( VarClient *pVarClient )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;
    VAR_HANDLE hVar;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        result = VARLIST_GetFirst( pVarClient->rr.clientid,
                                   pVarClient->rr.requestVal,
                                   &pVarClient->rr.variableInfo,
                                   &pVarClient->workbuf,
                                   pVarClient->workbufsize,
                                   &pVarClient->rr.len,
                                   &pVarClient->rr.responseVal);
        if( result == EINPROGRESS )
        {
            hVar = pVarClient->rr.variableInfo.hVar;

            /* add the client to the blocked clients list */
            BlockClient( pVarClient, NOTIFY_CALC, hVar );
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessVarRequestGetNext                                                  */
/*!
    Process a GET_NEXT variable request from a client

    The ProcessVarRequestGetNext function handles a "GET_NEXT variable"
    request from a client.  It continues a search for variables which
    match the specified search criteria.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the variable info was returned
    @retval ENOENT the variable was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessVarRequestGetNext( VarClient *pVarClient )
{
    int result = EINVAL;
    int rc;
    char *pStr = NULL;
    VAR_HANDLE hVar;

    /* validate the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK)
    {
        result = VARLIST_GetNext( pVarClient->rr.clientid,
                                  pVarClient->rr.requestVal,
                                  &pVarClient->rr.variableInfo,
                                  &pVarClient->workbuf,
                                  pVarClient->workbufsize,
                                  &pVarClient->rr.len,
                                  &pVarClient->rr.responseVal );
        if( result == EINPROGRESS )
        {
            hVar = pVarClient->rr.variableInfo.hVar;

            /* add the client to the blocked clients list */
            BlockClient( pVarClient, NOTIFY_CALC, hVar );
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessValidationRequest                                                  */
/*!
    Process a Validation Request from a client

    The ProcessValidationRequest function handles a validation request
    from a client.  A client sends a validation request to the server
    after it has received a validation signal from the server indicating
    that another client is trying to change a variable for which the
    client is registered as a validator.

    The Validation Request is used to retrieve the pending new value
    so it can be validated by the client.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the validation information was returned
    @retval ENOENT the validation request was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessValidationRequest( VarClient *pVarClient )
{
    int result = EINVAL;
    VarInfo *pVarInfo;
    pid_t peer_pid;
    VarClient *pSetClient;

    /* validated the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK )
    {
        /* get a pointer to the client requesting the validation */
        pSetClient = (VarClient *)TRANSACTION_Get( pVarClient->rr.requestVal );
        if( pSetClient != NULL )
        {
            if( pSetClient->rr.variableInfo.var.type == VARTYPE_STR )
            {
                /* strings will be stored in the client's working buffer */
                pVarClient->rr.variableInfo.var.val.str = &pVarClient->workbuf;
                pVarClient->rr.variableInfo.var.len = pVarClient->workbufsize;
            }

            if( pSetClient->rr.variableInfo.var.type == VARTYPE_BLOB )
            {
                /* blobs will be stored in the client's working buffer */
                pVarClient->rr.variableInfo.var.val.blob = &pVarClient->workbuf;


                // TO DO : Need to pass blob size as well as blob!!!!
                pVarClient->rr.variableInfo.var.len = pVarClient->workbufsize;
            }

            /* copy the variable handle from the setting client to the
               validating client */
            pVarClient->rr.variableInfo.hVar = pSetClient->rr.variableInfo.hVar;

            /* copy the Variable object from the setter to the validator */
            result = VAROBJECT_Copy( &pVarClient->rr.variableInfo.var,
                                     &pSetClient->rr.variableInfo.var );
        }
        else
        {
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  ProcessValidationResponse                                                 */
/*!
    Process a Validation Response from a client

    The ProcessValidationResponse function handles a validge is accepted
    or rejected.

    @param[in]
        pVarClient
            Pointer to the client data structure

    @retval EOK the validation information was returned
    @retval ENOENT the validation request was not found
    @retval EINVAL the client is invalid
    @retval ENOTSUP the client is the wrong version

==============================================================================*/
int ProcessValidationResponse( VarClient *pVarClient )
{
    int result = EINVAL;
    VarInfo *pVarInfo;
    VarClient *pSetClient;

    pid_t peer_pid;

    /* validated the client object */
    result = ValidateClient( pVarClient );
    if( result == EOK )
    {
        /* get a pointer to the client requesting the validation */
        pSetClient = (VarClient *)TRANSACTION_Remove(pVarClient->rr.requestVal);
        if( pSetClient != NULL )
        {
            /* copy the response from the validator to the setter */
            pSetClient->rr.responseVal = pVarClient->rr.responseVal;

            if( pVarClient->rr.responseVal == EOK )
            {
                /* set the value on behalf of the requestor */
                result = ProcessVarRequestSet( pSetClient );
            }

            /* unblock the client */
            UnblockClient( pSetClient );
        }
    }

    return result;

}

/*============================================================================*/
/*  AddRenderHandler                                                          */
/*!
    Add a new render handler

    The AddRenderHandler function adds a render handling function to
    the list of render handling functions.   Each render handling function
    is associated with a variable handle, and when a request is received
    to print that variable, the render handling function will be invoked.

    @param[in]
        hVar
            Variable handle associated with the render handling function

    @param[in]
        fn
            pointer to the render handling function

    @retval EOK the render handling function was registered successfully
    @retval ENOMEM memory allocation failure
    @retval EINVAL invalid arguments

==============================================================================*/
int AddRenderHandler( VAR_HANDLE hVar,
                      int (*fn)(VarInfo *pVarInfo, char *buf, size_t len) )
{
    int result = EINVAL;
    RenderHandler *pRenderHandler;

    if ( ( hVar != VAR_INVALID ) &&
         ( fn != NULL ) )
    {
        /* allocate memory for the RenderHandler */
        pRenderHandler = calloc( 1, sizeof( RenderHandler ) );
        if ( pRenderHandler != NULL )
        {
            /* populate the node */
            pRenderHandler->hVar = hVar;
            pRenderHandler->fn = fn;

            /* insert the node into the start of the render handlers list */
            pRenderHandler->pNext = pRenderHandlers;
            pRenderHandlers = pRenderHandler;

            result = EOK;
        }
        else
        {
            result = ENOMEM;
        }
    }

    return result;
}

/*! @}
 * end of handlers group */
