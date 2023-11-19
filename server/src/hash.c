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
 * @defgroup hash Hash Table Manager
 * @brief Hash Table Manager for mapping variable names to VarStorage
 * @{
 */

/*============================================================================*/
/*!
@file hash.c

    Hash Table Manager

    The Hash Table Manager maps variable names to their corresponding
    VarStorage object.

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <search.h>
#include <errno.h>
#include "hash.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*==============================================================================
        Private function declarations
==============================================================================*/

/*==============================================================================
        Public function definitions
==============================================================================*/

/*============================================================================*/
/*  HASH_Init                                                                 */
/*!
    Initialize the hash table

    The HASH_Init function initializes the hash table and prepares it for
    the expected number of elements.  The specified expected number
    should be 25% greater than the real expected number to ensure
    that collisions are minimized.

    @param[in]
        n
            Expected number of elements in the hash table

    @retval EOK the hash table was successfully initialized
    @retval ENOMEM the hash table could not be created
    @retval EINVAL invalid arguments

==============================================================================*/
int HASH_Init( size_t n )
{
    int result = EINVAL;

    if ( n > 0 )
    {
        /* create the hash table with the requested size */
        result = hcreate( n ) == 0 ? EOK : ENOMEM;
    }

    return result;
}

/*============================================================================*/
/*  HASH_Add                                                                  */
/*!
    Add an entry to the hash table

    The HASH_Add function adds a new entry to the hash table

    @param[in]
        name
            name of the object (used for object retrieval)

    @param[in]
        entry
            pointer to an object to add

    @retval EOK the object was added ok
    @retval EINVAL invalid arguments
    @retval ENOMEM object could not be added

==============================================================================*/
int HASH_Add( char *name, void *object )
{
    int result = EINVAL;
    ENTRY e;

    if ( ( name != NULL ) &&
         ( object != NULL ) )
    {
        e.key = strdup(name);
        if ( e.key != NULL )
        {
            e.data = object;

            if ( hsearch( e, ENTER ) != NULL )
            {
                result = EOK;
            }
            else
            {
                free( e.key );
                result = ENOMEM;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  HASH_Find                                                                 */
/*!
    Find an object in the hash table

    The HASH_Find function searches for the specified object in the
    hash table.

    @param[in]
        name
            name of the object to find

    @retval pointer to the object
    @retval NULL if the object cannot be found

==============================================================================*/
void *HASH_Find( char *name )
{
    void *p = NULL;
    ENTRY e;
    ENTRY *ep;

    if ( name != NULL )
    {
        e.key = name;

        /* search for the object in the hash table */
        ep = hsearch( e, FIND );
        if ( ep != NULL )
        {
            p = ep->data;
        }
    }

    return p;
}

/*! @}
 * end of hash group */
