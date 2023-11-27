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

#ifndef NOTIFY_H
#define NOTIFY_H

/*============================================================================
        Includes
============================================================================*/

#include <stdint.h>
#include <varserver/var.h>

/*============================================================================
        Public definitions
============================================================================*/

#ifndef EOK
#define EOK 0
#endif

/*! bitmask to indicate if the variable has an associated NOTIFY_MODIFIED notification */
#define NOTIFY_MASK_MODIFIED    ( 1 << 1 )

/*! bitmask to indicate if the variable has an associated NOTIFY_CALC notification */
#define NOTIFY_MASK_CALC        ( 1 << 2 )

/*! bitmask to indicate if the variable has an associated NOTIFY_VALIDATE notification */
#define NOTIFY_MASK_VALIDATE    ( 1 << 3 )

/*! bitmask to indicate if the variable has an associated NOTIFY_PRINT notification */
#define NOTIFY_MASK_PRINT       ( 1 << 4 )

/*! bitmask to indicate if the variable has clients blocked on a PRINT notification */
#define NOTIFY_MASK_HAS_PRINT_BLOCK ( 1 << 5 )

/*! bitmask to indicate if the variable has clients blocked on a CALC notification */
#define NOTIFY_MASK_HAS_CALC_BLOCK  ( 1 << 6 )

/*! bitmask to indicate if the variable has clients blocked on a VALIDATE notification */
#define NOTIFY_MASK_HAS_VALIDATE_BLOCK  ( 1 << 7 )

/*! bitmask to indiciate if a variable has clients with queue notification */
#define NOTIFY_MASK_MODIFIED_QUEUE ( 1 << 8 )

/*! The Notification object is used for storing notifications
    associated with each variable */
typedef struct _Notification
{
    /*! The identifier of the client requesting the notification */
    int clientID;

    /*! The PID of the process we need to notify */
    pid_t pid;

    /*! the notification queue descriptor */
    mqd_t mq;

    /*! message queue signal notification is pending */
    bool pending;

    /*! the variable handle associated with the notification request */
    VAR_HANDLE hVar;

    /*! The type of notification being requested */
    NotificationType type;

    /*! pointer to the next notification for this variable */
    struct _Notification *pNext;

} Notification;

/*============================================================================
        Public function declarations
============================================================================*/

int NOTIFY_Signal( pid_t pid,
                   Notification **ppNotification,
                   NotificationType type,
                   int handle,
                   pid_t *sentTo );

int NOTIFY_Payload( Notification **ppNotification,
                    void *buf,
                    size_t len );

Notification *NOTIFY_Find( Notification *pNotification,
                           NotificationType type,
                           pid_t pid );

int NOTIFY_Add( Notification **ppNotification,
                NotificationType type,
                VAR_HANDLE hVar,
                pid_t pid );

VAR_HANDLE NOTIFY_GetVarHandle( Notification *pNotification,
                                NotificationType type );

#endif
