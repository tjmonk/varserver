/*============================================================================
MIT License

Copyright (c) 2025

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

#ifndef GC_H
#define GC_H

/*============================================================================
        Includes
============================================================================*/

#include <signal.h>
#include <stddef.h>
#include <varserver/varclient.h>

/*============================================================================
        Public definitions
============================================================================*/

/*! Timer Signal used by GC */
#define SIG_GC_TIMER ( SIGRTMIN + 6 )

/*============================================================================
        Public function declarations
============================================================================*/

/* Initialize GC timer (10s interval) */
void GC_Initialize(void);

/* Run GC over the client table */
void GC_Process( VarClient **table, size_t max_clients );

#endif /* GC_H */
