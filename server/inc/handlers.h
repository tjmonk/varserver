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

#ifndef HANDLERS_H
#define HANDLERS_H

/*============================================================================
        Includes
============================================================================*/

#include <stdint.h>
#include <varserver/var.h>
#include <varserver/varclient.h>

/*============================================================================
        Public definitions
============================================================================*/

/*============================================================================
        Public function declarations
============================================================================*/

int InitHandlerMetrics( void );
int HandleRequest( VarClient *pVarClient );
int UnblockClient( VarClient *pVarClient );
int ValidateClient( VarClient *pVarClient );
int ProcessVarRequestInvalid( VarClient *pVarClient );
int ProcessVarRequestOpen( VarClient *pVarClient );
int ProcessVarRequestClose( VarClient *pVarClient );
int ProcessVarRequestEcho( VarClient *pVarClient );
int ProcessVarRequestNew( VarClient *pVarClient );
int ProcessVarRequestFind( VarClient *pVarClient );
int ProcessVarRequestPrint( VarClient *pVarClient );
int ProcessVarRequestSet( VarClient *pVarClient );
int ProcessVarRequestType( VarClient *pVarClient );
int ProcessVarRequestName( VarClient *pVarClient );
int ProcessVarRequestLength( VarClient *pVarClient );
int ProcessVarRequestGet( VarClient *pVarClient );
int ProcessVarRequestNotify( VarClient *pVarClient );
int ProcessValidationRequest( VarClient *pVarClient );
int ProcessValidationResponse( VarClient *pVarClient );
int ProcessVarRequestOpenPrintSession( VarClient *pVarClient );
int ProcessVarRequestClosePrintSession( VarClient *pVarClient );
int ProcessVarRequestGetFirst( VarClient *pVarClient );
int ProcessVarRequestGetNext( VarClient *pVarClient );

int AddRenderHandler( VAR_HANDLE hVar,
                      int (*fn)(VarInfo *pVarInfo, char *buf, size_t len) );

#endif
