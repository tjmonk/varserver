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

/*!
 * @defgroup validate validate
 * @brief Manages a list in-progress validations
 * @{
 */

/*==========================================================================*/
/*!
@file validate.c

    Validate

    The Validate List manages a list of in-progress data validations

*/
/*==========================================================================*/

/*============================================================================
        Includes
============================================================================*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include "varclient.h"
#include "validate.h"

/*============================================================================
        Private definitions
============================================================================*/


/*============================================================================
        Private types
============================================================================*/

/*! the ValidationRequest object defines a validation request */
typedef struct _ValidationRequest
{
    /*! the request handle is a unique identifier which
        represents this validation request */
    uint32_t requestHandle;

    /*! the clientPID is the process identifier of the client which is
        requesting a change to the variable */
    pid_t clientPID;

    /*! the pVarInfo pointer points to the Variable Info which contains
        the proposed change to the variable */
    VarInfo *pVarInfo;

    /*! the pNext pointer points to the next validation request object
        in the list of validation request objects */
    struct _ValidationRequest *pNext;

} ValidationRequest;

/*============================================================================
        Private function declarations
============================================================================*/

/*============================================================================
        Private file scoped variables
============================================================================*/

/*! blocked clients */
static ValidationRequest *validationRequestList = NULL;

/*! list of available ValidationRequest objects */
static ValidationRequest *freelist = NULL;

/*! Validation Request Counter */
static uint32_t RequestCounter = 0L;

/*============================================================================
        Public function definitions
============================================================================*/

/*==========================================================================*/
/*  VALIDATE_Request                                                        */
/*!
    Send a Validation Request to the validation client

    The VALIDATE_Request function creates a new validation request
    object and returns a handle to it

    @param[in]
        clientPID
            the process identifier of the client requesting validation

    @param[in]
        pVarInfo
            pointer to the VarInfo object containing the proposed new value

    @param[out]
        pHandle
            pointer to a location to store the validation request handle

    @retval EOK the validation request was successfully created
    @retval EINVAL invalid arguments
    @retval ENOMEM memory allocation problem

============================================================================*/
int VALIDATE_Request( pid_t clientPID,
                      VarInfo *pVarInfo,
                      uint32_t *pHandle )
{
    uint32_t result = EINVAL;
    ValidationRequest *pValidationRequest;

    if( ( pVarInfo != NULL ) &&
        ( pHandle != NULL ) )
    {
        if( freelist != NULL )
        {
            /* get a new ValidationRequest object from the free list */
            pValidationRequest = freelist;
            freelist = freelist->pNext;
        }
        else
        {
            /* allocate a new ValidationRequest object */
            pValidationRequest = calloc( 1, sizeof( ValidationRequest ) );
        }

        if( pValidationRequest != NULL )
        {
            /* populate the ValidationReqeuest object */
            pValidationRequest->clientPID = clientPID;
            pValidationRequest->pVarInfo = pVarInfo;
            pValidationRequest->requestHandle = ++RequestCounter;

            /* insert the ValidationRequest on the head of the
               ValidationRequest list */
            pValidationRequest->pNext = validationRequestList;
            validationRequestList = pValidationRequest;

            result = EOK;
        }
        else
        {
            result = ENOMEM;
        }
    }

    return result;
}

/*==========================================================================*/
/*  VALIDATE_GetRequest                                                     */
/*!
    Get a validation request given its request identifier

    The VALIDATE_GetRequest function gets the validation request information
    consisting of a client PID and a VarInfo object given a validation
    request identifier.

    @param[in]
        clientPID
            the process identifier of the client requesting validation

    @param[in]
        pVarInfo
            pointer to the VarInfo object containing the proposed new value

    @param[out]
        pHandle
            pointer to a location to store the validation request handle

    @retval EOK the validation request information was successfully retrieved
    @retval EINVAL invalid arguments
    @retval ENOENT validation request was not found

============================================================================*/
int VALIDATE_GetRequest( uint32_t requestHandle,
                         pid_t *pPID,
                         VarInfo **ppVarInfo )
{
    int result = EINVAL;
    ValidationRequest *pValidationRequest;
    ValidationRequest *pPrevRequest = validationRequestList;

    if( ( pPID != NULL ) &&
        ( ppVarInfo != NULL ) )
    {
        pValidationRequest = validationRequestList;
        while( pValidationRequest != NULL )
        {
            if( pValidationRequest->requestHandle == requestHandle )
            {
                /* get the request information */
                *pPID = pValidationRequest->clientPID;
                *ppVarInfo = pValidationRequest->pVarInfo;

                /* remove the validation request from the validation
                   request list and put it into the free list */
                if( pPrevRequest == validationRequestList )
                {
                    /* remove the validation request from
                       the head of the validation request list */
                    validationRequestList = pValidationRequest->pNext;
                }
                else
                {
                    /* remove the validation request from the interior
                       of the validation request list */
                    pPrevRequest->pNext = pValidationRequest->pNext;
                }

                /* clear the validation request */
                pValidationRequest->clientPID = -1;
                pValidationRequest->pVarInfo = NULL;

                /* move the validation request to the free list */
                pValidationRequest->pNext = freelist;
                freelist = pValidationRequest;

            }

            pPrevRequest = pValidationRequest;
            pValidationRequest = pValidationRequest->pNext;
        }

        result = ( pValidationRequest == NULL ) ? ENOENT : EOK;
    }

    return result;
}


/*! @}
 * end of validate group */
