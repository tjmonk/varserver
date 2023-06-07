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

#ifndef VARCACHE_H
#define VARCACHE_H

/*============================================================================
        Includes
============================================================================*/

#include <sys/types.h>
#include "var.h"

/*============================================================================
        Public definitions
============================================================================*/

#ifndef EOK
#define EOK 0
#endif

/*! default Maximum Variable Cache size */
#define VARCACHE_DEFAULT_SIZE       ( 100 )

/*! default size to grow the cache by when it is full */
#define VARCACHE_DEFAULT_GROW_BY    ( 100 )

/*! opaque Variable Cache object */
typedef struct _VarCache VarCache;

/*============================================================================
        Public function declarations
============================================================================*/

int VARCACHE_Init( VarCache **ppVarCache, size_t len, size_t growBy );

size_t VARCACHE_Size( VarCache *pVarCache );

bool VARCACHE_HasVar( VarCache *pVarCache, VAR_HANDLE hVar );

int VARCACHE_Add( VarCache *pVarCache, VAR_HANDLE hVar );

int VARCACHE_AddUnique( VarCache *pVarCache, VAR_HANDLE hVar );

VAR_HANDLE VARCACHE_Get( VarCache *pVarCache, size_t idx );

int VARCACHE_Map( VarCache *pVarCache,
                  int (*fn)(VAR_HANDLE hVar, void *arg),
                  void *arg );

int VARCACHE_Clear( VarCache *pVarCache );

#endif
