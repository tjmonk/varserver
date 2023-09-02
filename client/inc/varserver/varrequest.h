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

#ifndef VARREQUEST_H
#define VARREQUEST_H

/*============================================================================
        Includes
============================================================================*/

/*============================================================================
        Public definitions
============================================================================*/

/*! identifier for the var server */
#define VARSERVER_ID  ( 0x56415253 )

/*! protocol version of the var server to check for library/server mismatch */
#define VARSERVER_VERSION ( 1 )

/*! the VarRequest enumeration specifies the type of requests that can
    be made from the client to the server */
typedef enum _varRequest
{
    /*! invalid VAR request */
    VARREQUEST_INVALID=0,

    /*! A new client is requesting an interface to the variable server */
    VARREQUEST_OPEN,

    /*! A client is requesting to terminate its interface to the variable server */
    VARREQUEST_CLOSE,

    /*! echo test */
    VARREQUEST_ECHO,

    /*! New Variable request */
    VARREQUEST_NEW,

    /*! Find Variable request */
    VARREQUEST_FIND,

    /*! Get Variable value */
    VARREQUEST_GET,

    /*! Print Variable Value */
    VARREQUEST_PRINT,

    /*! Set Variable Value */
    VARREQUEST_SET,

    /*! Get Variable Type */
    VARREQUEST_TYPE,

    /*! Get Variable Name */
    VARREQUEST_NAME,

    /*! Get Variable Length */
    VARREQUEST_LENGTH,

    /*! Request Variable Notification */
    VARREQUEST_NOTIFY,

    /*! Request Validation Request Info */
    VARREQUEST_GET_VALIDATION_REQUEST,

    /*! Send a Validation Response */
    VARREQUEST_SEND_VALIDATION_RESPONSE,

    /*! Open a print session */
    VARREQUEST_OPEN_PRINT_SESSION,

    /*! Close a print session */
    VARREQUEST_CLOSE_PRINT_SESSION,

    /*! Get the first variable in a search query result */
    VARREQUEST_GET_FIRST,

    /*! Get the next variable in a search query result */
    VARREQUEST_GET_NEXT,

    /*! End request type marker */
    VARREQUEST_END_MARKER

} VarRequest;

#endif
