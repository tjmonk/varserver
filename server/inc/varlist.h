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

#ifndef VARLIST_H
#define VARLIST_H

/*============================================================================
        Includes
============================================================================*/

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include <varserver/var.h>

/*============================================================================
        Public definitions
============================================================================*/

/*============================================================================
        Public function declarations
============================================================================*/

int VARLIST_AddNew( VarInfo *pVarInfo, uint32_t *pVarHandle );
int VARLIST_Find( VarInfo *pVarInfo, VAR_HANDLE *pVarHandle );
int VARLIST_PrintByHandle( int client_id,
                           VarInfo *pVarInfo,
                           char *workbuf,
                           size_t workbufsize,
                           size_t *len,
                           void *clientInfo,
                           int *handler );

int VARLIST_Set( int client_id,
                 VarInfo *pVarInfo,
                 bool *validationInProgress,
                 void *clientInfo );

int VARLIST_GetType( VarInfo *pVarInfo );
int VARLIST_GetName( VarInfo *pVarInfo );
int VARLIST_GetLength( VarInfo *pVarInfo );
int VARLIST_RequestNotify( int client_id,
                           VarInfo *pVarInfo,
                           pid_t pid,
                           int sd );
int VARLIST_GetByHandle( int client_id,
                         VarInfo *pVarInfo,
                         char *buf,
                         size_t bufsize,
                         size_t *len );

int VARLIST_GetFirst( int client_id,
                      int searchType,
                      VarInfo *pVarInfo,
                      char *buf,
                      size_t bufsize,
                      size_t *len,
                      int *context );

int VARLIST_GetNext( int client_id,
                     int context,
                     VarInfo *pVarInfo,
                     char *buf,
                     size_t bufsize,
                     size_t *len,
                     int *response );

VarObject *VARLIST_GetObj( VAR_HANDLE hVar );

#endif
