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
 * @defgroup metric variable server metrics
 * @brief Functions for managing variable server metrics
 * @{
 */

/*============================================================================*/
/*!
@file metric.c

    Variable Server Metrics

    The Variable Server Metrics create internal metrics variables for reporting
    varserver statistics and metrics.

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
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syslog.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include <varserver/varclient.h>
#include <varserver/varserver.h>
#include "varlist.h"
#include "metric.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*==============================================================================
        Private types
==============================================================================*/

/*==============================================================================
        Private function declarations
==============================================================================*/

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*============================================================================*/
/*  MakeMetric                                                                */
/*!
    Create a 64-bit metric counter

    The MakeMetric function creates a new 64-bit unsigned integer for
    use as a metric counter.  It returns a pointer to the 64 bit value
    that can be quickly accessed to update the metric value.

    @param[in]
        name
            pointer to the name of the metric to create

    @retval pointer to the 64-bit metric value
    @retval NULL if the metric could not be created

==============================================================================*/
uint64_t *MakeMetric( char *name )
{
    uint64_t *p = NULL;
    VarInfo info;
    VAR_HANDLE hVar;
    VarObject *pVarObject;
    size_t len;

    /* create the metric variable */
    memset(&info, 0, sizeof(VarInfo));
    len = sizeof(info.name);
    strncpy(info.name, name, len);
    info.name[len-1] = 0;
    info.var.len = sizeof( uint64_t );
    info.var.type = VARTYPE_UINT64;
    VARLIST_AddNew( &info, &hVar );

    /* get a pointer to the metric variable */
    pVarObject = VARLIST_GetObj( hVar );
    if ( pVarObject != NULL )
    {
        /* get a pointer to the metric value */
        p = &(pVarObject->val.ull);
    }

    return p;
}

/*============================================================================*/
/*  MakeStringMetric                                                          */
/*!
    Make a string metric variable

    The MakeStringMetric function creates a string variable for reporting
    VarServer string metrics.  It returns a handle to the new variable.

    @param[in]
        name
            pointer to the name of the metric to create

    @param[in]
        len
            the length of the string buffer to allocate for the string variable

    @retval handle to the VarServer variable
    @retval VAR_INVALID if the variable could not be created

==============================================================================*/
VAR_HANDLE MakeStringMetric( char *name, size_t len )
{
    VarInfo info;
    VAR_HANDLE hVar = VAR_INVALID;
    size_t l;

    /* create the string metric variable */
    memset(&info, 0, sizeof(VarInfo));
    l = sizeof(info.name);
    strncpy(info.name, name, l);
    info.name[l-1] = 0;
    info.var.len = len;
    info.var.val.str = calloc(1, len);
    info.var.type = VARTYPE_STR;
    VARLIST_AddNew( &info, &hVar );

    return hVar;
}

/*! @}
 * end of metric group */
