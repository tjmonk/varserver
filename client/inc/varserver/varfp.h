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

#ifndef VARFP_H
#define VARFP_H

/*============================================================================
        Includes
============================================================================*/

#include <stdio.h>
#include <sys/types.h>

/*============================================================================
        Public definitions
============================================================================*/

#ifndef EOK
/*! Success response */
#define EOK 0
#endif

/*! Var File Pointer type */
typedef struct _VarFP VarFP;

/*============================================================================
        Public function declarations
============================================================================*/

VarFP *VARFP_Open( char *name, size_t len );

int VARFP_Close( VarFP *pVarFP );

FILE *VARFP_GetFP( VarFP *pVarFP );

int VARFP_GetFd( VarFP *pVarFP );

char *VARFP_GetData( VarFP *pVarFP );

size_t VARFP_GetSize( VarFP *pVarFP );

#endif
