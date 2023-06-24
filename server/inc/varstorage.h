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

#ifndef VARSTORAGE_H
#define VARSTORAGE_H

/*============================================================================
        Includes
============================================================================*/

#include <varserver/varclient.h>
#include <varserver/var.h>
#include "notify.h"

/*============================================================================
        Public definitions
============================================================================*/

#ifndef MAX_TAGS_LEN
/*! maximum number of tags per variable */
#define MAX_TAGS_LEN        ( 8 )
#endif

/*! The VarStorage object is used internally by the varserver to
    encapsulate all the information about a single variable */
typedef struct _VarStorage
{
    /*! instance identifier for this variable */
    uint32_t instanceID;

    /*! name of the variable */
    char name[MAX_NAME_LEN+1];

    /*! globally unique identifier for the variable */
    uint32_t guid;

    /*! variable data */
    VarObject var;

    /*! variable flags */
    VarFlags flags;

    /*! variable tag specifiers */
    uint16_t tags[MAX_TAGS_LEN];

    /*! variable format specifier */
    char formatspec[MAX_FORMATSPEC_LEN];

    /*! variable permissions */
    VarPermissions permissions;

    /*! pointer to the notification list for this variable */
    Notification *pNotifications;

    /* indicate if this variable has an associated CALC notification */
    uint8_t notifyMask;

} VarStorage;


#endif
