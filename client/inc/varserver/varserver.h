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
#ifndef VARSERVER_H
#define VARSERVER_H

/*============================================================================
        Includes
============================================================================*/

#include "var.h"
#include "varprint.h"
#include <signal.h>

/*============================================================================
        Public definitions
============================================================================*/

#ifndef VARSERVER_DEFAULT_WORKBUF_SIZE
/*! default working buffer size for the variable client/server */
#define VARSERVER_DEFAULT_WORKBUF_SIZE  ( BUFSIZ )
#endif

#ifndef VARSERVER_MAX_NOTIFICATION_MSG_SIZE
/*! default size of notification messages */
#define VARSERVER_MAX_NOTIFICATION_MSG_SIZE    ( 4096 )
#endif

#ifndef VARSERVER_MAX_NOTIFICATION_MSG_COUNT
/*! default max number of notification messages per client */
#define VARSERVER_MAX_NOTIFICATION_MSG_COUNT   ( 10 )
#endif

/*! signal indicating a timer has fired  */
#define SIG_VAR_TIMER    ( SIGRTMIN + 5 )

/*! signal indicating a variable has been modified */
#define SIG_VAR_MODIFIED ( SIGRTMIN + 6 )

/*! signal indicating a variable calculation is required */
#define SIG_VAR_CALC     ( SIGRTMIN + 7 )

/*! signal indicating a variable validation is required */
#define SIG_VAR_VALIDATE ( SIGRTMIN + 8 )

/*! signal indicating a variable print is required */
#define SIG_VAR_PRINT    ( SIGRTMIN + 9 )

/*! signal indicating the variable notification queue has been modified */
#define SIG_VAR_QUEUE_MODIFIED ( SIGRTMIN + 10 )

/*! handle to the variable server */
typedef void * VARSERVER_HANDLE;

/*! The VarNotification object is used to retrieve
    variable notifications from the Variable Server
    via the Notification message queue */
typedef struct _VarNotification
{
    /*! variable handle */
    VAR_HANDLE hVar;

    /*! variable object containing TLV data */
    VarObject obj;

} VarNotification;

/*============================================================================
        Public function declarations
============================================================================*/

VARSERVER_HANDLE VARSERVER_Open( void );
VARSERVER_HANDLE VARSERVER_OpenExt( size_t workbufsize );
int VARSERVER_Debug( VARSERVER_HANDLE hVarServer, int debug );
int VARSERVER_GetWorkingBuffer( VARSERVER_HANDLE hVarServer,
                                char **pBuf,
                                size_t *pLen );
int VARSERVER_Close( VARSERVER_HANDLE hVarServer );
int VARSERVER_Test( VARSERVER_HANDLE hVarServer );
int VARSERVER_CreateVar( VARSERVER_HANDLE hVarServer,
                         VarInfo *pVarInfo );
int VARSERVER_WaitSignal( int *sigval );

/* Flag functions */
int VARSERVER_StrToFlags( char *flagsString,
                          uint32_t *flags );

int VARSERVER_FlagsToStr( uint32_t flags,
                          char *flagsString,
                          size_t len );

/* Permission functions */
int VARSERVER_ParsePermissionSpec( char *permissionSpec,
                                   uint16_t *permissions,
                                   size_t len );

/* type functions */
int VARSERVER_TypeNameToType( char *typeName, VarType *type );
int VARSERVER_TypeToTypeName( VarType type, char *typeName, size_t len );

/* data value functions */
int VARSERVER_ParseValueString( VarObject *var, char *valueString );

/* variable functions */
VAR_HANDLE VAR_FindByName( VARSERVER_HANDLE hVarServer, char *pName );

int VAR_GetLength( VARSERVER_HANDLE hVarServer,
                   VAR_HANDLE hVar,
                   size_t *len );

int VAR_Print( VARSERVER_HANDLE hVarServer,
               VAR_HANDLE hVar,
               int fd );

int VAR_Get( VARSERVER_HANDLE hVarServer,
             VAR_HANDLE hVar,
             VarObject *pVarObject );

int VAR_GetStrByName( VARSERVER_HANDLE hVarServer,
                      char *name,
                      char *buf,
                      size_t len );

int VAR_GetBlobByName( VARSERVER_HANDLE hVarServer,
                       char *name,
                       void *buf,
                       size_t len );

int VAR_GetType( VARSERVER_HANDLE hVarServer,
                 VAR_HANDLE hVar,
                 VarType *pVarType );

int VAR_GetName( VARSERVER_HANDLE hVarServer,
                 VAR_HANDLE hVar,
                 char *buf,
                 size_t len );

int dumpmem( uint8_t *p, size_t n);

int VAR_Set( VARSERVER_HANDLE hVarServer,
             VAR_HANDLE hVar,
             VarObject *pVarObject );

int VAR_SetStr( VARSERVER_HANDLE hVarServer,
                VAR_HANDLE hVar,
                VarType type,
                char *str );

int VAR_SetNameValue( VARSERVER_HANDLE hVarServer,
                      char *name,
                      char *value );

int VARSERVER_WaitSignal( int *sigval );

sigset_t VARSERVER_SigMask( void );

int VAR_Notify( VARSERVER_HANDLE hVarServer,
                VAR_HANDLE hVar,
                NotificationType notificationType );

int VAR_GetValidationRequest( VARSERVER_HANDLE hVarServer,
                              uint32_t id,
                              VAR_HANDLE *hVar,
                              VarObject *pVarObject );

int VAR_SendValidationResponse( VARSERVER_HANDLE hVarServer,
                                uint32_t id,
                                int response  );

int VAR_OpenPrintSession( VARSERVER_HANDLE hVarServer,
                         uint32_t id,
                         VAR_HANDLE *hVar,
                         int *fd );

int VAR_ClosePrintSession( VARSERVER_HANDLE hVarServer,
                           uint32_t id,
                           int fd );

int VAR_GetFirst( VARSERVER_HANDLE hVarServer,
                  VarQuery *query,
                  VarObject *obj );
int VAR_GetNext( VARSERVER_HANDLE hVarServer,
                 VarQuery *query,
                 VarObject *obj );

int VARSERVER_CreateClientQueue( VARSERVER_HANDLE hVarServer,
                                 long queuelen,
                                 long msgsize );

int VAR_GetFromQueue( VARSERVER_HANDLE hVarServer,
                      VarNotification *pVarNotification,
                      char *buf,
                      size_t len );

int VARSERVER_Signalfd( void );

int VARSERVER_WaitSignalfd( int fd, int32_t *sigval );

#endif
