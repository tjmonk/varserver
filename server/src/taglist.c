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
 * @defgroup taglist taglist
 * @brief RealTime In-Memory Publish/Subscribe Key/Value tag list
 * @{
 */

/*============================================================================*/
/*!
@file taglist.c

    Tag List

    The Tag List maintains a tag enumeration mapping from tag number to
    tag name, and provides the capability to:
        - create a new tag
        - search for a tag number by its name
        - get a tag name by its number

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
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>
#include <strings.h>
#include <varserver/var.h>
#include <varserver/varserver.h>
#include "taglist.h"

/*==============================================================================
        Private definitions
==============================================================================*/

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*! counts the number of variables in the list */
static uint16_t tagcount = 0;

/*! variable storage */
static char *TagStorage[ VARSERVER_MAX_TAGS + 1 ] = {NULL};

/*==============================================================================
        Private function declarations
==============================================================================*/

static int taglist_Find( char *pTagName, uint16_t *pTagNumber );
static int taglist_Add( char *pTagName, uint16_t *pTagNumber );

/*==============================================================================
        Public function definitions
==============================================================================*/

/*============================================================================*/
/*  TAGLIST_AddNew                                                            */
/*!
    Add a new tag to the tag list

    The TAGLIST_AddNew function adds a new tag to the tag list
    It returns a tag number for the newly added tag.  If the tag
    already exists, the tag number for the existing tag is returned.
    The tag name is compared in a case insensitve manner.

    @param[in]
        pTagName
            Pointer to the name of the tag to add

    @param[in]
        pTagNumber
            pointer to a a location to store the tag number

    @retval EOK the tag variable was added
    @retval ENOMEM memory allocation failure
    @retval ENOSPC the tag database is full
    @retval EINVAL invalid arguments

==============================================================================*/
int TAGLIST_AddNew( char *pTagName, uint16_t *pTagNumber )
{
    int result = EINVAL;

    if( ( pTagName != NULL ) &&
        ( pTagNumber != NULL ) )
    {
        /* see if the tag already exists */
        result = taglist_Find( pTagName, pTagNumber );
        if( result == ENOENT )
        {
            /* the tag does not exist and there is still
               space left in the tag database */
            result = taglist_Add( pTagName, pTagNumber );
        }
    }

    return result;
}

/*============================================================================*/
/*  TAGLIST_Parse                                                             */
/*!
    Parse a comma separated tag name string into a tag number array

    The TAGLIST_Parse function parses a comma separated tag name string
    (tagspec) into a tag number array, adding any new tags to the
    taglist storage as it goes.

    @param[in]
        pTagspec
            Pointer to the comma separated tag spec list of tag names

    @param[in]
        pTags
            pointer to the output tag number array

    @param[in]
        len
            length of the tag number array

    @retval EOK the tagspec string was successfully parsed
    @retval E2BIG the tag spec string length exceeds the maximum size
    @retval ENOSPC the tag database is full
    @retval ENOMEM memory allocation failure
    @retval EINVAL invalid arguments

==============================================================================*/
int TAGLIST_Parse( char *pTagspec, uint16_t *pTags, size_t len )
{
    int result = EINVAL;
    char buf[MAX_TAGSPEC_LEN + 1];
    size_t tagSpecLen;
    uint16_t tagNumber;
    char *r;
    char *pTagName;
    int rc;
    size_t count = 0;

    if( ( pTagspec != NULL ) &&
        ( pTags != NULL ) )
    {
        /* get the length of the tagspec string */
        tagSpecLen = strlen(pTagspec);
        if( tagSpecLen <= MAX_TAGSPEC_LEN )
        {
            /* assume success until we determine otherwise */
            result = EOK;

            /* copy the tagspec into our working buffer */
            strcpy( buf, pTagspec );
            r = buf;
            while( ( pTagName = strtok_r( r, ",", &r ) ) != NULL )
            {
                /* check if we have enough space in the tags output array */
                if( count < len )
                {
                    /* try to add the tag */
                    rc = TAGLIST_AddNew( pTagName, &tagNumber );
                    if( rc == EOK )
                    {
                        /* store the tag number in the tags output array */
                        pTags[count++] = tagNumber;
                    }
                    else
                    {
                        /* something went wrong, capture the error
                           and abort */
                        result = rc;
                        break;
                    }
                }
                else
                {
                    /* we have run out of space in the output buffer
                       capture the error and abort */
                    result = ENOSPC;
                    break;
                }
            }
        }
        else
        {
            /* the tagspec string is too big */
            result = E2BIG;
        }
    }

    return result;
}

/*============================================================================*/
/*  TAGLIST_TagsToString                                                      */
/*!
    Convert a tag number array into a string of tag names

    The TAGLIST_TagsToString function converts a tag number array
    into a comma separated list of tag names.

    @param[in]
        pTags
            Pointer to the tag number array

    @param[in]
        len
            length of the tag number array

    @param[in]
        pBuf
            pointer to the output buffer

    @param[in]
        n
            size of the output buffer

    @retval EOK the tags string was successfully created
    @retval E2BIG not enough space for the tag string in the output buffer
    @retval ENOENT one of the specified tags does not exist
    @retval EINVAL invalid arguments

==============================================================================*/
int TAGLIST_TagsToString( uint16_t *pTags, size_t len, char *pBuf, size_t n )
{
    size_t i;
    int result = EINVAL;
    uint16_t tagNumber;
    char tagName[MAX_TAGSPEC_LEN];
    size_t tagNameLen;
    size_t remaining = n;
    size_t offset = 0;
    int rc;

    if( ( pTags != NULL ) &&
        ( pBuf != NULL ) )
    {
        /* assume success until something fails */
        result = EOK;

        /* insert initial NUL for the case where the tag number array
           is empty */
        *pBuf = 0;

        /* iterate through the tag number array */
        for( i = 0; ( ( i < len ) && ( pTags[i] != 0 ) ) ; i++ )
        {
            tagNumber = pTags[i];
            rc = TAGLIST_GetTagName( tagNumber, tagName, MAX_TAGSPEC_LEN );
            if( rc == EOK )
            {
                /* get the length of the tag name including NUL terminator */
                tagNameLen = strlen(tagName);
                tagNameLen++;

                /* check if this is the first tag */
                if( i > 0 )
                {
                    /* not first tag, allow space for a prepended comma */
                    tagNameLen++;
                }

                /* check that we have enough space in the output buffer */
                if( tagNameLen <= remaining )
                {
                    if( i > 0 )
                    {
                        /* prepend a comma */
                        pBuf[offset++] = ',';
                    }

                    /* append the tag name */
                    sprintf( &pBuf[offset], "%s", tagName );

                    /* update the offset and remaining values */
                    offset += tagNameLen;
                    remaining -= tagNameLen;
                }
                else
                {
                    /* we have run out of space in the output buffer */
                    result = E2BIG;
                    break;
                }
            }
            else
            {
                /* tag does not exist */
                result = rc;
                break;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  TAGLIST_GetTagNumber                                                      */
/*!
    Get the tag number given its name

    The TAGLIST_GetTagNumber function retrieves a tag number
    given its name.

    @param[in]
        pTagName
            Pointer to the name of the tag to add

    @param[in]
        pTagNumber
            pointer to a a location to store the tag number

    @retval EOK the tag number was retrieved
    @retval ENOENT the tag was not found
    @retval EINVAL invalid arguments

==============================================================================*/
int TAGLIST_GetTagNumber( char *pTagName, uint16_t *pTagNumber )
{
    return taglist_Find( pTagName, pTagNumber );
}

/*============================================================================*/
/*  TAGLIST_GetTagName                                                        */
/*!
    Get a TagName given its tag number

    The TAGLIST_GetTagName function gets the tag name associated
    with the specified tag number.

    @param[in]
        tagNumber
            the tag number to look up

    @param[out]
        pTagName
            pointer to a location to store the tag name

    @param[in]
        size
            size of the buffer to store the tag name

    @retval EOK the tag name was retrieved
    @retval ENOENT the tag does not exist
    @retval E2BIG the tag name will not fit in the buffer provided
    @retval EINVAL invalid arguments

==============================================================================*/
int TAGLIST_GetTagName( uint16_t tagNumber, char *pTagName, size_t size )
{
    int result = EINVAL;
    char *pTag;
    size_t len;

    if( pTagName != NULL )
    {
        if( ( tagNumber > 0 ) &&
            ( tagNumber <= tagcount ) )
        {
            /* get a pointer to the tag name in the tag storage */
            pTag = TagStorage[tagNumber];
            if( pTag != NULL )
            {
                /* get the length of the tag in storage */
                len = strlen( pTag );
                if( size > len )
                {
                    /* copy the tag name the the supplied buffer */
                    strcpy( pTagName, TagStorage[tagNumber] );
                    result = EOK;
                }
                else
                {
                    /* the tag name is too big for the supplied buffer */
                    result = E2BIG;
                }

            }
        }
        else
        {
            result = ENOENT;
        }
    }

    return result;
}

/*==============================================================================
        Private function definitions
==============================================================================*/

/*============================================================================*/
/*  taglist_Find                                                              */
/*!
    Find a tag number given its name

    The taglist_Find function searches for the specified tag name
    in the tag list and returns the tag number.

    @param[in]
        pTagName
            Pointer to the name of the tag to add

    @param[in]
        pTagNumber
            pointer to a a location to store the tag number

    @retval EOK the tag number was found
    @retval ENOENT the tag was not found
    @retval EINVAL invalid arguments

==============================================================================*/
static int taglist_Find( char *pTagName, uint16_t *pTagNumber )
{
    int result = EINVAL;
    uint16_t tagNumber = 1;

    if( ( pTagName != NULL ) &&
        ( pTagNumber != NULL ) )
    {
        /* assume we do not find it until we do */
        result = ENOENT;

        while( ( tagNumber < VARSERVER_MAX_TAGS ) &&
                ( TagStorage[tagNumber] != NULL ) )
        {
            /* perform a case insensitve string comparison */
            if( strcasecmp( TagStorage[tagNumber], pTagName) == 0 )
            {
                *pTagNumber = tagNumber;
                result = EOK;
                break;
            }

            /* move to the next tag */
            tagNumber++;
        }
    }

    return result;
}

/*============================================================================*/
/*  taglist_Add                                                               */
/*!
    Add a tag at the end of the list

    The taglist_Add function adds a new tag at the end
    of the tag list and returns the tag number.

    @param[in]
        pTagName
            Pointer to the name of the tag to add

    @param[in]
        pTagNumber
            pointer to a a location to store the tag number

    @retval EOK the tag was added
    @retval ENOMEM memory allocation failure
    @retval EINVAL invalid arguments

==============================================================================*/
static int taglist_Add( char *pTagName, uint16_t *pTagNumber )
{
    int result = EINVAL;
    char *pNewTag;
    size_t len;

    if( ( pTagName != NULL ) &&
        ( pTagNumber != NULL ) )
    {
        /* check that we have enough space for the new tag */
        if( tagcount < VARSERVER_MAX_TAGS )
        {
            /* get the length of the new tag */
            len = strlen( pTagName );

            /* allocate memory for the new tag including a NULL terminator */
            pNewTag = malloc( len + 1 );
            if( pNewTag != NULL )
            {
                /* copy the tag name to the tag storage */
                strcpy( pNewTag, pTagName );
                TagStorage[++tagcount] = pNewTag;
                *pTagNumber = tagcount;

                /* tag successfully added */
                result = EOK;
            }
            else
            {
                /* unable to allocate memory for the tag name */
                result = ENOMEM;
            }
        }
        else
        {
            /* the tag database is full */
            result = ENOSPC;
        }
    }

    return result;
}

/*! @}
 * end of varlist group */
