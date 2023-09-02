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

#ifndef SOCKAPI_H
#define SOCKAPI_H

/*============================================================================
        Includes
============================================================================*/

#include <stdint.h>
#include "varclient.h"
#include "varrequest.h"
#include "var.h"

/*============================================================================
        Public definitions
============================================================================*/

typedef struct _SockRequest
{
    /*! identifier of the var client */
    uint32_t id;

    /*! varserver version */
    uint16_t version;

    /*! client identifier */
    uint32_t clientID;

    /*! transaction identifier */
    uint32_t transaction_id;

    /*! type of request */
    VarRequest requestType;

    /*! request Value */
    int requestVal;

    /*! request Value 2 */
    int requestVal2;

} SockRequest;

typedef struct _SockResponse
{
    /*! identifier of the var client */
    uint32_t id;

    /*! varserver version */
    uint16_t version;

    /*! type of request */
    VarRequest requestType;

    /*! transaction identifier */
    uint32_t transaction_id;

    /*! response Value */
    int responseVal;

    /*! response Value 2*/
    int responseVal2;

} SockResponse;

typedef struct _PrintResponse
{
    VAR_HANDLE hVar;

    char formatspec[MAX_FORMATSPEC_LEN];

    VarObject obj;

    int responseVal;

    size_t len;

} PrintResponse;

/*============================================================================
        Public function declarations
============================================================================*/

const VarServerAPI *SOCKAPI( void );

#endif
