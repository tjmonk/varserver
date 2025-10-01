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

#ifndef STATS_H
#define STATS_H

/*============================================================================
        Includes
============================================================================*/

#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

/*============================================================================
        Public definitions
============================================================================*/
/*! Timer Signal */
#define SIG_STATS_TIMER                 ( SIGRTMIN + 5 )

/*============================================================================
        File Scoped Variables
============================================================================*/

/*============================================================================
        Public function declarations
============================================================================*/

void STATS_Initialize( void );
void STATS_IncrementRequestCount( void );
void STATS_Process( void );
void STATS_SetRequestsPerSecPtr( uint64_t *p );
void STATS_SetTotalRequestsPtr( uint64_t *p );
void STATS_SetGCCleanedPtr( uint64_t *p );
void STATS_IncrementGCCleaned( void );
void STATS_SetGCCleanedPtr( uint64_t *p );
void STATS_IncrementGCCleaned( void );

#endif
