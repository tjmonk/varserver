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
 * @defgroup notify notify
 * @brief Variable Notification List Manager
 * @{
 */

/*============================================================================*/
/*!
@file notify.c

    Variable Notification List Manager

    The Variable Notification List Manager maintains a list of
    notifications associated with a variable.

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
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include <mqueue.h>
#include <varserver/var.h>
#include "varstorage.h"
#include "notify.h"
#include "stats.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*==============================================================================
        Private function declarations
==============================================================================*/

static int notify_Send( Notification *pNotification,
                        int handle,
                        int signal );

static mqd_t notify_GetQueue( pid_t pid );

/*==============================================================================
        Public function definitions
==============================================================================*/

/*============================================================================*/
/*  NOTIFY_Add                                                                */
/*!
    Add a new notification request

    The NOTIFY_Add function adds a new notification request to
    the specified variable notification list.

    @param[in,out]
        ppNotificaiton
            Pointer to the notification request list

    @param[in]
        type
            notification type

    @param[in]
        pid
            process id of the client to be notified

    @retval EOK the notification was successfully added
    @retval ENOTSUP the notification is not supported
    @retval EINVAL invalid arguments

==============================================================================*/
int NOTIFY_Add( Notification **ppNotification,
                NotificationType type,
                pid_t pid )
{
    int result = EINVAL;
    Notification *pNotification;

    if( ppNotification != NULL )
    {
        /* get a pointer to the first notification in the notification list */
        pNotification = *ppNotification;
        switch( type )
        {
            case NOTIFY_MODIFIED_QUEUE:
            case NOTIFY_MODIFIED:
                /* check if we are already registered */
                pNotification = NOTIFY_Find( pNotification, type, pid );
                break;

            case NOTIFY_VALIDATE:
            case NOTIFY_CALC:
            case NOTIFY_PRINT:
                /* only one validator/calculator allowed */
                pNotification = NOTIFY_Find( pNotification, type, -1 );
                break;

            default:
                result = ENOTSUP;
                break;
        }

        if( result != ENOTSUP )
        {
            /* notification type is supported */
            if( pNotification == NULL )
            {
                /* try to find an unused (abandoned) notification */
                pNotification = NOTIFY_Find( *ppNotification,
                                             NOTIFY_NONE,
                                             -1 );
                if( pNotification == NULL )
                {
                    /* a matching notification was not found
                    so let's create one */
                    pNotification = calloc( 1, sizeof( Notification ) );
                    if( pNotification != NULL )
                    {
                        /* insert the new notification
                        at the head of the notification list */
                        pNotification->pNext = *ppNotification;
                        *ppNotification = pNotification;
                    }
                }
            }

            if( pNotification != NULL )
            {
                if ( type == NOTIFY_MODIFIED_QUEUE )
                {
                    pNotification->mq = notify_GetQueue( pid );
                }

                /* populate the notification structure */
                pNotification->pid = pid;
                pNotification->type = type;

                /* notification successfully registered */
                result = EOK;
            }
            else
            {
                result = ENOMEM;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  NOTIFY_Find                                                               */
/*!
    Find an existing notification request

    The NOTIFY_Find function searches for an existing notification
    request which has the same type (and pid). If the specified pid
    is -1, it is not used in the search and the first notification
    of the specified type found in the notification list is returned.

    @param[in]
        pNotificaiton
            Pointer to the first notification in the notification request list

    @param[in]
        type
            notification type

    @param[in]
        pid
            process id of the client to be notified

    @retval pointer to the matching notification
    @retval NULL if there was no matching notification

==============================================================================*/
Notification *NOTIFY_Find( Notification *pNotification,
                           NotificationType type,
                           pid_t pid )
{
    while( pNotification != NULL )
    {
        if( ( pNotification->type == type ) && ( pid == -1 ) )
        {
            break;
        }
        else if( ( pNotification->type == type ) &&
                 ( pNotification->pid == pid ) )
        {
            break;
        }

        pNotification = pNotification->pNext;
    }

    return pNotification;
}

/*============================================================================*/
/*  NOTIFY_Signal                                                             */
/*!
    Send a notification signal to all registered clients

    The NOTIFY_Signal function sends out a notification signal
    to each client in the notification list which is registered
    for the specified notification type.  Once a single CALC, VALIDATE,
    or PRINT notification is processed, we exit since
    each variable can only have one of each of those notification types.

    @param[in]
        pid
            process identifier of the process which is initiating the signal

    @param[in]
        pNotification
            Pointer to the first notification in the notification request list

    @param[in]
        type
            notification type

    @param[in]
        handle
            notification handle

    @param[in]
        sentTo
            location to store PID of the receiving process

    @retval EOK all notifications were sent
    @retval ESRCH one or more processed did not exist
    @retval EINVAL invalid arguments

==============================================================================*/
int NOTIFY_Signal( pid_t pid,
                   Notification **ppNotification,
                   NotificationType type,
                   int handle,
                   pid_t *sentTo )
{
    int result = EINVAL;
    Notification *pNotification;
    int sig = -1;
    int done = 0;

    if( ppNotification != NULL )
    {
        /* select the first notification */
        pNotification = *ppNotification;

        result = ENOENT;

        while( ( pNotification != NULL ) && ( ! done ) )
        {
            if( pNotification->type == type )
            {
                switch(type)
                {
                    case NOTIFY_MODIFIED_QUEUE:
                        if ( pNotification->pending == true )
                        {
                            sig = SIGRTMIN+10;
                            pNotification->pending = false;
                        }
                        else
                        {
                            /* don't send this notification because
                               the message queue payload was not sent */
                            sig = -1;
                        }
                        break;

                    case NOTIFY_MODIFIED:
                        sig = SIGRTMIN+6;
                        break;

                    case NOTIFY_CALC:
                        if( pNotification->pid == pid )
                        {
                            /* don't send a CALC signal to the processs
                            which is registered to perform the calculation */
                            sig = -1;
                        }
                        else
                        {
                            sig = SIGRTMIN+7;
                        }
                        done = 1;
                        break;

                    case NOTIFY_VALIDATE:
                        if( pNotification->pid == pid )
                        {
                            /* don't send a VALIDATE signal to the process
                            which is registered to perform the validation */
                            sig = -1;
                        }
                        else
                        {
                            sig = SIGRTMIN+8;
                        }
                        done = 1;
                        break;

                    case NOTIFY_PRINT:
                        if( pNotification->pid == pid )
                        {
                            /* don't send a PRINT signal to the process which is
                               registered to perform the PRINT operation */
                            sig = -1;
                        }
                        else
                        {
                            sig = SIGRTMIN+9;
                        }
                        done = 1;
                        break;

                    default:
                        break;
                }

                if( sig != -1 )
                {
                    /* send the notification */
                    result = notify_Send( pNotification, handle, sig );
                    if( result == EOK )
                    {
                        if( sentTo != NULL )
                        {
                            *sentTo = pNotification->pid;
                        }
                    }
                }
            }

            /* select the next notification */
            pNotification = pNotification->pNext;
        }
    }

    return result;
}

/*============================================================================*/
/*  notify_Send                                                               */
/*!
    Send a notification signal

    The notify_Send function sends out a notification signal
    to the client referenced in the notification object.

    If the client does not exist, the notification object is marked
    as unused

    @param[in]
        pNotification
            pointer to the notification to send

    @param[in]
        handle
            notification handle

    @param[in]
        signal
            signal to send

    @retval EOK the notifications was sent
    @retval ESRCH the process which reqeusted the notification does not exist
    @retval EINVAL invalid arguments

==============================================================================*/
static int notify_Send( Notification *pNotification,
                        int handle,
                        int signal )
{
    int result = EINVAL;
    union sigval val;
    int rc;

    if( pNotification != NULL )
    {
        /* provide the signal handle to the var server */
        val.sival_int = handle;

        /* queue the notification */
        rc = sigqueue( pNotification->pid, signal, val );
        if( rc == -1 )
        {
            result = errno;
            if( result == ESRCH )
            {
                /* the process that registered this signal is gone,
                mark the signal as unused */
                pNotification->type = NOTIFY_NONE;
                pNotification->pid = -1;
            }
        }
        else
        {
            result = EOK;
        }
    }
    else
    {
        result = ENOENT;
    }

    return result;
}

/*============================================================================*/
/*  NOTIFY_Payload                                                            */
/*!
    Send a notification payload to a client's message queue

    The NOTIFY_Payload function sends a notification payload to each client
    which has registered to receive it.

    @param[in]
        ppNotification
            pointer to the Notification list

    @param[in]
        buf
            pointer to the notification payload to send

    @param[in]
        len
            length of the payload to send

    @retval EOK at least one notification was sent
    @retval EINVAL invalid arguments
    @retval ENOENT no notifications registered

==============================================================================*/
int NOTIFY_Payload( Notification **ppNotification,
                    void *buf,
                    size_t len )
{
    int result = EINVAL;
    int rc;
    Notification *pNotification;

    if( ppNotification != NULL )
    {
        /* select the first notification */
        pNotification = *ppNotification;

        result = ENOENT;

        while( pNotification != NULL )
        {
            if( pNotification->type == NOTIFY_MODIFIED_QUEUE )
            {
                /* send the message to the clients message queue */
                rc = mq_send( pNotification->mq, buf, len, 0);
                if ( rc == 0 )
                {
                    /* update the request stats */
                    STATS_IncrementRequestCount();

                    pNotification->pending = true;
                    result = EOK;
                }
                else
                {
                    rc = errno;
                    if ( rc == EBADF )
                    {
                        /* the process that requested this notification is gone,
                        mark the signal as unused */
                        pNotification->type = NOTIFY_NONE;
                        pNotification->pid = -1;
                    }
                }
            }

            /* select the next notification */
            pNotification = pNotification->pNext;
        }
    }

    return result;
}


/*============================================================================*/
/*  notify_GetQueue                                                           */
/*!
    Get the notification queue for a client process

    The notify_GetQueue function gets the notification queue associated
    with the client specified via its pid.

    @param[in]
        pid
            process identifier of the client process

    @retval message queue descriptor
    @retval -1 if the message queue does not exist

==============================================================================*/
static mqd_t notify_GetQueue( pid_t pid )
{
    char clientname[BUFSIZ];
    mqd_t mq;

    /* build the varclient identifier */
    sprintf(clientname, "/varclient_%d", pid);

    mq = mq_open( clientname, O_WRONLY | O_NONBLOCK );
    if ( mq == -1 )
    {
        printf("Failed to open %s : %s\n", clientname, strerror(errno));
    }
    return mq;
}

/*! @}
 * end of notify group */
