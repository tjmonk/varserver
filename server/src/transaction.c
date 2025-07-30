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
 * @defgroup transaction transaction
 * @brief Manages a list of in-progress transactions
 * @{
 */

/*============================================================================*/
/*!
@file transaction.c

    Transaction

    The Transaction List manages a list of in-progress transactions
    between clients

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
#include <sys/syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include <varserver/var.h>
#include "transaction.h"

/*==============================================================================
        Private definitions
==============================================================================*/
#ifndef EOK
#define EOK 0
#endif

/*==============================================================================
        Private types
==============================================================================*/

/*! the Transaction object tracks an inprogress transaction between clients */
typedef struct _Transaction
{
    /*! the transaction identifier is a unique identifier which
        represents this transaction between clients */
    uint32_t transactionID;

    /*! the requestor is the process identifier of the client which
        initiated the transaction */
    pid_t requestor;

    /*! handle to the variable associated with the transaction */
    VAR_HANDLE hVar;

    /*! opaque pointer to the transaction information */
    void *pInfo;

    /*! the pNext pointer points to the next transaction object
        in the list of transaction objects */
    struct _Transaction *pNext;

} Transaction;

/*==============================================================================
        Private function declarations
==============================================================================*/

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! list of active transactions */
static Transaction *transactionList = NULL;

/*! list of available Transaction objects */
static Transaction *freelist = NULL;

/*! Transaction Counter used to generate unique transaction identifiers */
static uint32_t TransactionCounter = 0L;

/*==============================================================================
        Public function definitions
==============================================================================*/

/*============================================================================*/
/*  TRANSACTION_New                                                           */
/*!
    Create a new transaction

    The TRANSACTION_New function creates a new transaction object
    object and returns a handle to it

    @param[in]
        clientPID
            the process identifier of the client initiating the transaction

    @param[in]
        pData
            pointer to the opaque data object associated with the new
            transaction

    @param[in]
        hVar
            handle to the variable associated with the transaction

    @param[out]
        pHandle
            pointer to a location to store the transaction handle

    @retval EOK the transaction object was successfully created
    @retval EINVAL invalid arguments
    @retval ENOMEM memory allocation problem

==============================================================================*/
int TRANSACTION_New( pid_t clientPID,
                     void *pData,
                     VAR_HANDLE hVar,
                     uint32_t *pHandle )
{
    uint32_t result = EINVAL;
    Transaction *pTransaction;

    if( ( pData != NULL ) &&
        ( pHandle != NULL ) )
    {
        if( freelist != NULL )
        {
            /* get a new ValidationRequest object from the free list */
            pTransaction = freelist;
            freelist = freelist->pNext;
        }
        else
        {
            /* allocate a new Transaction object */
            pTransaction = calloc( 1, sizeof( Transaction ) );
        }

        if( pTransaction != NULL )
        {
            /* populate the ValidationReqeuest object */
            pTransaction->requestor = clientPID;
            pTransaction->pInfo = pData;
            pTransaction->hVar = hVar;
            pTransaction->transactionID = ++TransactionCounter;
            *pHandle = pTransaction->transactionID;

            /* insert the Transaction on the head of the
               Transaction list */
            pTransaction->pNext = transactionList;
            transactionList = pTransaction;

            result = EOK;
        }
        else
        {
            result = ENOMEM;
        }
    }

    return result;
}

/*============================================================================*/
/*  TRANSACTION_Get                                                           */
/*!
    Get a transaction given its transaction identifier

    The TRANSACTION_Get function gets the transaction information
    associated with the specified transaction identifier

    @param[in]
        transactionID
            the transaction identifier to search for

    @param[in,out]
        hVar
            pointer to the location to store the variable handle associated
            with the transaction

    @retval pointer to the transaction information
    @retval NULL the transaction identifier was not found

==============================================================================*/
void *TRANSACTION_Get( uint32_t transactionID, VAR_HANDLE *hVar )
{
    Transaction *pTransaction;
    void *pTransactionInfo = NULL;

    pTransaction = transactionList;

    while( pTransaction != NULL )
    {
        if( pTransaction->transactionID == transactionID )
        {
            pTransactionInfo = pTransaction->pInfo;

            if ( hVar != NULL )
            {
                *hVar = pTransaction->hVar;
            }

            break;
        }

        pTransaction = pTransaction->pNext;
    }

    return pTransactionInfo;
}

/*============================================================================*/
/*  TRANSACTION_FindByRequestor                                               */
/*!
    Get a transaction given its requestor identifier

    The TRANSACTION_FindByRequestor function gets the transaction information
    associated with the specified requestor identifier

    @param[in]
        requestor
            the transaction identifier to search for

    @param[in,out]
        hVar
            pointer to the location to store the variable handle associated
            with the transaction

    @retval pointer to the transaction information
    @retval NULL the transaction was not found

==============================================================================*/
void *TRANSACTION_FindByRequestor( pid_t requestor, VAR_HANDLE *hVar )
{
    Transaction *pTransaction;
    void *pTransactionInfo = NULL;

    pTransaction = transactionList;

    while( pTransaction != NULL )
    {
        if( pTransaction->requestor == requestor )
        {
            pTransactionInfo = pTransaction->pInfo;

            if ( hVar != NULL )
            {
                *hVar = pTransaction->hVar;
            }
            break;
        }

        pTransaction = pTransaction->pNext;
    }

    return pTransactionInfo;
}

/*============================================================================*/
/*  TRANSACTION_Remove                                                        */
/*!
    Remove a transaction given its transaction identifier

    The TRANSACTION_Remove function gets the transaction information
    associated with the specified transaction identifier and removes
    the transaction from the active transactions list

    @param[in]
        transactionID
            the transaction identifier to search for

    @retval pointer to the transaction information
    @retval NULL the transaction identifier was not found

==============================================================================*/
void *TRANSACTION_Remove( uint32_t transactionID )
{
    Transaction *pTransaction = transactionList;
    Transaction *pPrevTransaction = NULL;
    void *pTransactionInfo = NULL;

    while( pTransaction != NULL )
    {
        if( pTransaction->transactionID == transactionID )
        {
            /* get a pointer to the transaction info */
            pTransactionInfo = pTransaction->pInfo;

            /* remove the transaction from the transaction list
               and put it into the free list */
            if( pPrevTransaction == NULL )
            {
                /* remove the transaction from the head of
                   the transaction list */
                transactionList = pTransaction->pNext;
            }
            else
            {
                /* remove the transaction from the interior
                    of the transaction list */
                pPrevTransaction->pNext = pTransaction->pNext;
            }

            /* clear the transaction object */
            pTransaction->requestor = -1;
            pTransaction->pInfo = NULL;
            pTransaction->transactionID = 0L;

            /* move the transaction to the free list */
            pTransaction->pNext = freelist;
            freelist = pTransaction;

            break;
        }

        pPrevTransaction = pTransaction;

        /* move to the next transaction */
        pTransaction = pTransaction->pNext;
    }

    return pTransactionInfo;
}

/*! @}
 * end of transaction group */
