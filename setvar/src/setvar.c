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
 * @defgroup setvar setvar
 * @brief Set Variable value into the In-Memory Pub/Sub Key/Value store
 * @{
 */

/*============================================================================*/
/*!
@file setvar.c

    Set Variable

    The Set Variable Application sets the value for the specified
    variable in the variable server

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include "varserver.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*==============================================================================
        Private function declarations
==============================================================================*/

/*============================================================================*/
/*  main                                                                      */
/*!
    Main entry point for the setvar application

    The main function starts the setvar application

    @param[in]
        argc
            number of arguments on the command line
            (including the command itself)

    @param[in]
        argv
            array of pointers to the command line arguments

    @return none

==============================================================================*/
void main(int argc, char **argv)
{
    VARSERVER_HANDLE hVarServer = NULL;
    VAR_HANDLE hVar;
    VarType type;
    int result;

    if( argc != 3 )
    {
        printf("usage: setvar <varname> <value>\n");
        exit( 1 );
    }

    /* get a handle to the VAR server */
    hVarServer = VARSERVER_Open();
    if( hVarServer != NULL )
    {
        result = VAR_SetNameValue( hVarServer, argv[1], argv[2] );
        if( result != EOK )
        {
            fprintf(stderr, "SETVAR: %s\n", strerror( result ) );
        }
        else
        {
            printf("OK\n");
        }
    }

    /* close the variable server */
    VARSERVER_Close( hVarServer );

    if( result != EOK )
    {
        fprintf( stderr, "%s\n", strerror( result ) );
    }
}

/*! @}
 * end of setvar group */
