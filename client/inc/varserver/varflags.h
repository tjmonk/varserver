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

#ifndef VARFLAGS_H
#define VARFLAGS_H

/*============================================================================
        Includes
============================================================================*/

#include <stdbool.h>
#include <stdint.h>
#include "varserver.h"
#include "var.h"

/*============================================================================
        Defines
============================================================================*/

/*============================================================================
        Public Types
============================================================================*/

/*! operation type to set or clear the flag */
typedef enum _flagModifyOperation
{
    /*! clear the flag */
    FlagClear = 0,

    /* set the flag */
    FlagSet = 1,
} FlagModifyOperation;

/*============================================================================
        Public Function Declarations
============================================================================*/

int VAR_ModifyFlags( VARSERVER_HANDLE hVarServer,
                     char *match,
                     uint32_t flags,
                     FlagModifyOperation op );

int VAR_SetFlags( VARSERVER_HANDLE hVarServer,
                  VAR_HANDLE hVar,
                  uint32_t flags );

int VAR_ClearFlags( VARSERVER_HANDLE hVarServer,
                    VAR_HANDLE hVar,
                    uint32_t flags );

int VAR_ClearDirtyFlags( VARSERVER_HANDLE hVarServer );



#endif
