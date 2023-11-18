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

#ifndef VAR_H
#define VAR_H

/*============================================================================
        Includes
============================================================================*/

#include <stdint.h>
#include "varobject.h"

/*============================================================================
        Public definitions
============================================================================*/

#ifndef EOK
#define EOK 0
#endif

/*! maximum length of the variable name string */
#ifndef MAX_NAME_LEN
#define MAX_NAME_LEN  ( 63 )
#endif

/*! maximum number of uids for the read/write permissions */
#ifndef VARSERVER_MAX_UIDS
#define VARSERVER_MAX_UIDS     ( 6 )
#endif

/*! maximum client group ids */
#ifndef VARSERVER_MAX_CLIENT_GIDS
#define VARSERVER_MAX_CLIENT_GIDS  ( 20 )
#endif

/*! maximum format specifier length */
#ifndef MAX_FORMATSPEC_LEN
#define MAX_FORMATSPEC_LEN  ( 8 )
#endif

/*! maximum tag specifier length */
#ifndef MAX_TAGSPEC_LEN
#define MAX_TAGSPEC_LEN     ( 128 )
#endif

/*! maximum number of tags per variable */
#ifndef MAX_TAGS_LEN
#define MAX_TAGS_LEN        ( 8 )
#endif

/*! maximum flag specifier length */
#ifndef MAX_FLAGSPEC_LEN
#define MAX_FLAGSPEC_LEN   ( 128 )
#endif

/*! maximum length of the permission specifier string */
#ifndef MAX_PERMISSIONSPEC_LEN
#define MAX_PERMISSIONSPEC_LEN  ( 64 )
#endif

/*! invalid variable handle */
#define VAR_INVALID     ( 0 )

/*! handle to a variable stored in the variable server */
typedef uint32_t VAR_HANDLE;

/*! Regular Expression query */
#define QUERY_REGEX ( 1 << 0 )

/*! case sensitive match query */
#define QUERY_MATCH  ( 1 << 1 )

/*! flags match */
#define QUERY_FLAGS ( 1 << 2 )

/*! tags match */
#define QUERY_TAGS  ( 1 << 3 )

/*! instanceID match */
#define QUERY_INSTANCEID ( 1 << 4 )

/*! query output value */
#define QUERY_SHOWVALUE ( 1 << 5 )

/*! Variable flags */
typedef enum _VarFlags
{
    /*! No flag */
    VARFLAG_NONE = 0,

    /*! Volatile variable (do not save) */
    VARFLAG_VOLATILE = 1,

    /*! read only constant */
    VARFLAG_READONLY = 2,

    /*! hidden variable */
    VARFLAG_HIDDEN = 4,

    /*! dirty variable */
    VARFLAG_DIRTY = 8,

    /*! public variable */
    VARFLAG_PUBLIC = 16,

    /*! trigger variable (value not changed) */
    VARFLAG_TRIGGER = 32,

    /*! variable auditing */
    VARFLAG_AUDIT = 64,

    /*! password variable */
    VARFLAG_PASSWORD = 128

} VarFlags;

/*! Variable permissions */
typedef struct _VarPermissions
{
    /*! number of read permissions */
    size_t nreads;

    /*! read permissions */
    gid_t read[VARSERVER_MAX_UIDS];

    /*! number of write permissions */
    size_t nwrites;

    /*! write permissions */
    gid_t write[VARSERVER_MAX_UIDS];

} VarPermissions;


/*! The NotificationType enumeration is used when requesting
     a notification of an action or request for action for
     a variable */
typedef enum _NotificationType
{
    /*! no notification required */
    NOTIFY_NONE = 0,

    /*! notify AFTER a variable is modified */
    NOTIFY_MODIFIED = 1,

    /*! request for calculation of a variable */
    NOTIFY_CALC = 2,

    /*! request for validation of a variable */
    NOTIFY_VALIDATE = 3,

    /*! request for printing of a variable */
    NOTIFY_PRINT = 4,

    /*! request for queue notification */
    NOTIFY_MODIFIED_QUEUE = 5

} NotificationType;


/*! The VarInfo object is used to contain variable information for
    interaction with the variable server */
typedef struct _VarInfo
{
    /*! variable handle */
    VAR_HANDLE hVar;

    /*! variable instance identifier */
    uint32_t instanceID;

    /*! name of the variable */
    char name[MAX_NAME_LEN+1];

    /*! globally unique identifier for the variable */
    uint32_t guid;

    /*! variable data */
    VarObject var;

    /*! variable flags */
    uint32_t flags;

    /*! variable tag specifier */
    char tagspec[MAX_TAGSPEC_LEN];

    /*! variable format specifier */
    char formatspec[MAX_FORMATSPEC_LEN];

    /*! variable permissions */
    VarPermissions permissions;

    /*! notification type */
    NotificationType notificationType;

    /*! user credentials */
    gid_t creds[ VARSERVER_MAX_CLIENT_GIDS ];

    /* number of credentials */
    size_t ncreds;

} VarInfo;

/*! VarQuery object used to search for variables by
 *  name match, flags match, or tags match */
typedef struct _VarQuery
{
    /*! search context */
    int context;

    /*! query type */
    int type;

    /*! instance ID match */
    uint32_t instanceID;

    /*! search match string */
    char *match;

    /*! search results must contain all these flags */
    uint32_t flags;

    /*! search results must contain all these tags */
    char tagspec[MAX_TAGSPEC_LEN];

    /*! OUT: name of the variable */
    char name[MAX_NAME_LEN+1];

    /*! OUT: Variable handle */
    VAR_HANDLE hVar;
} VarQuery;

#endif
