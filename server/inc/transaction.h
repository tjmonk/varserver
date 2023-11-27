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

#ifndef TRANSACTION_H
#define TRANSACTION_H

/*============================================================================
        Includes
============================================================================*/

#include <stdint.h>
#include <sys/types.h>
#include <varserver/var.h>

/*============================================================================
        Public function declarations
============================================================================*/

int TRANSACTION_New( pid_t clientPID,
                     void *pData,
                     VAR_HANDLE hVar,
                     uint32_t *pHandle );

void *TRANSACTION_Get( uint32_t transactionID, VAR_HANDLE *hVar );

void *TRANSACTION_Remove( uint32_t transactionID );

void *TRANSACTION_FindByRequestor( pid_t requestor, VAR_HANDLE *hVar );

#endif