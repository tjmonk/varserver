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
 * @defgroup varobject varobject
 * @brief Manage the VarObject data type
 * @{
 */

/*============================================================================*/
/*!
@file varobject.c

    Variable Object Management

    The Variable Object Management module provides APIs for manipulating
    the VarObject datatype which is used to store variable data in
    the real time in-memory pub/sub key/value store,

    Both clients and the variable server itself can use these APIs to
    manipulate the VarObject data type.

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <semaphore.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include <inttypes.h>
#include <varserver/varobject.h>

/*==============================================================================
        Private data types
==============================================================================*/

/*! structure for managing VarObject conversion */
typedef struct _VarObjectHandler
{
    /* the type of the VarObject */
    VarType type;

    /*! type name */
    char *typeName;

    /*! pointer to the function to create a VarObject from a string */
    int (*strtovar)( char *str,
                     VarObject *pVarObject,
                     uint32_t options );

    /*! pointer to the function to convert a VarObject to a string */
    int (*vartostr)( char *str,
                     size_t len,
                     char *fmt,
                     VarObject *pVarObject,
                     uint32_t options );

    /*! pointer to the function to write the object to a file descriptor */
    int (*vartofd)( VarObject *pVarObject,
                    char *fmt,
                    int fd,
                    uint32_t options );

    /*! pointer to the function to write the object to a stream */
    int( *vartostream)( VarObject *pVarObject,
                        char *fmt,
                        FILE *fp,
                        uint32_t options );

} VarObjectHandler;

/*==============================================================================
        Private function declarations
==============================================================================*/

static int uint16str_to_var( char *str,
                             VarObject *pVarObject,
                             uint32_t options );

static int int16str_to_var( char *str,
                            VarObject *pVarObject,
                            uint32_t options );

static int uint32str_to_var( char *str,
                             VarObject *pVarObject,
                             uint32_t options );

static int int32str_to_var( char *str,
                            VarObject *pVarObject,
                            uint32_t options );

static int uint64str_to_var( char *str,
                             VarObject *pVarObject,
                             uint32_t options );

static int int64str_to_var( char *str,
                            VarObject *pVarObject,
                            uint32_t options );

static int floatstr_to_var( char *str,
                            VarObject *pVarObject,
                            uint32_t options );

static int str_to_var( char *str,
                       VarObject *pVarObject,
                       uint32_t options );

static int blobstr_to_var( char *str,
                           VarObject *pVarObject,
                           uint32_t options );

static int varobject_CopyString( VarObject *pDst, VarObject *pSrc );

static int varobject_CopyBlob( VarObject *pDst, VarObject *pSrc );

static bool check_positive_integer( char *str );

/*==============================================================================
        File scoped variables
==============================================================================*/

/*! the varHandlers array contains references to all of the VarObject
    conversion functions.  They have to be defined in the same order
    as they are defined in the VarType definition so they can be looked
    up quickly based on the variable type */
VarObjectHandler varHandlers[] =
{
    { VARTYPE_INVALID,
      "invalid",
      NULL, /* create a VarObject from a string */
      NULL, /* create a string from a VarObject */
      NULL, /* write VarObject to file descriptor */
      NULL }, /* write VarObject to stream */

    { VARTYPE_UINT16,
      "uint16",
      uint16str_to_var, /* create a VarObject from a string */
      NULL, /* create a string from a VarObject */
      NULL, /* write VarObject to file descriptor */
      NULL }, /* write VarObject to stream */

    { VARTYPE_INT16,
      "int16",
      int16str_to_var, /* create a VarObject from a string */
      NULL, /* create a string from a VarObject */
      NULL, /* write VarObject to file descriptor */
      NULL }, /* write VarObject to stream */

    { VARTYPE_UINT32,
      "uint32",
      uint32str_to_var, /* create a VarObject from a string */
      NULL, /* create a string from a VarObject */
      NULL, /* write VarObject to file descriptor */
      NULL }, /* write VarObject to stream */

    { VARTYPE_INT32,
      "int32",
      int32str_to_var, /* create a VarObject from a string */
      NULL, /* create a string from a VarObject */
      NULL, /* write VarObject to file descriptor */
      NULL }, /* write VarObject to stream */

    { VARTYPE_UINT64,
      "uint64",
      uint64str_to_var, /* create a VarObject from a string */
      NULL, /* create a string from a VarObject */
      NULL, /* write VarObject to file descriptor */
      NULL }, /* write VarObject to stream */

    { VARTYPE_INT64,
      "int64",
      int64str_to_var, /* create a VarObject from a string */
      NULL, /* create a string from a VarObject */
      NULL, /* write VarObject to file descriptor */
      NULL }, /* write VarObject to stream */

    { VARTYPE_FLOAT,
      "float",
      floatstr_to_var, /* create a VarObject from a string */
      NULL, /* create a string from a VarObject */
      NULL, /* write VarObject to file descriptor */
      NULL }, /* write VarObject to stream */

    { VARTYPE_STR,
      "str",
      str_to_var, /* create a VarObject from a string */
      NULL, /* create a string from a VarObject */
      NULL, /* write VarObject to file descriptor */
      NULL }, /* write VarObject to stream */

    { VARTYPE_BLOB,
      "blob",
      blobstr_to_var, /* create a VarObject from a string */
      NULL, /* create a string from a VarObject */
      NULL, /* write VarObject to file descriptor */
      NULL }, /* write VarObject to stream */

    { VARTYPE_END_MARKER,
      NULL,
      NULL, /* create a VarObject from a string */
      NULL, /* create a string from a VarObject */
      NULL, /* write VarObject to file descriptor */
      NULL } /* write VarObject to stream */
};

/*==============================================================================
        Function definitions
==============================================================================*/

/*============================================================================*/
/*  VAROBJECT_CreateFromStr                                                   */
/*!
    Convert a string variable into a VarObject

    The VAROBJECT_CreateFromStr function converts the specified string
    into a VarObject depending on the conversion type requested.

    @param[in]
        str
            pointer to a NUL terminated string containing the value to convert

    @param[in]
        type
            the type of the converted value

    @param[out]
        pVarObject
            pointer to the VarObject to contain the result

    @param[in]
        options
            conversion options which may be OR'd together to
            control the conversion behavior

            VAROBJECT_OPTION_COPY - indicates the string should be copied
                                    to a newly allocated buffer (in the case
                                    of a VARTYPE_STR type)

    @retval EOK - the VarObject was created ok
    @retval ENOTSUP - no conversion function available for this type
    @retval ENOMEM - memory allocation failed
    @retval ERANGE - variable out of range
    @retval EINVAL - invalid arguments

==============================================================================*/
int VAROBJECT_CreateFromString( char *str,
                                VarType type,
                                VarObject *pVarObject,
                                uint32_t options )
{
    int result = EINVAL;

    if( ( str != NULL ) &&
        ( pVarObject != NULL ) )
    {
        if( ( type > VARTYPE_INVALID ) &&
            ( type < VARTYPE_END_MARKER ) )
        {
            /* invoke the conversion function */
            result = varHandlers[type].strtovar( str, pVarObject, options );
        }
        else
        {
            result = ENOTSUP;
        }
    }

    if( ( result != EOK ) &&
        ( pVarObject != NULL ) )
    {
        /* clear the VarObject */
        memset( pVarObject, 0, sizeof( VarObject ));
    }

    return result;
}

/*============================================================================*/
/*  VAROBJECT_ValueFromStr                                                    */
/*!
    Assign a value to a VarObject from a string

    The VAROBJECT_ValueFromStr function parses the specified string
    value and assigns it into the specified VarObject depending on the
    object type.

    @param[in]
        str
            pointer to a NUL terminated string containing the value to convert

    @param[in]
        pVarObject
            pointer to the VarObject to assign the value to

    @param[in]
        options
            conversion options which may be OR'd together to
            control the conversion behavior

            VAROBJECT_OPTION_COPY - indicates the string should be copied
                                    to a newly allocated buffer (in the case
                                    of a VARTYPE_STR type)

    @retval EOK - the VarObject was assigned ok
    @retval ENOTSUP - no conversion function available for this type
    @retval ENOMEM - memory allocation failed
    @retval ERANGE - variable out of range
    @retval EINVAL - invalid arguments

==============================================================================*/
int VAROBJECT_ValueFromString( char *str,
                               VarObject *pVarObject,
                               uint32_t options )
{
    int result = EINVAL;
    VarType type;

    if( ( str != NULL ) &&
        ( pVarObject != NULL ) )
    {
        type = pVarObject->type;
        if( ( type > VARTYPE_INVALID ) &&
            ( type < VARTYPE_END_MARKER ) )
        {
            /* invoke the conversion function */
            result = varHandlers[type].strtovar( str, pVarObject, options );
        }
        else
        {
            result = ENOTSUP;
        }
    }

    return result;
}

/*============================================================================*/
/*  VAROBJECT_Copy                                                            */
/*!
    Copy one VarObject to another

    The VAROBJECT_Copy function is used to copy one VarObject to another.

    If the object type is a string, and the destination object already
    points to a string buffer, then that string buffer will be used.
    If the destination object does not have a string buffer, then one
    will be allocated for it.

    If the object type is a blob, and the destination object already
    points to a blob buffer, then that blob buffer will be used.
    If the destination object does not have a blob buffer, then one will
    be allocated for it.

    @param[in]
        pDst
            pointer to the destination VarObject

    @param[in]
        pSrc
            pointer to the source VarObject

    @retval EOK - the VarObject was copied ok
    @retval ENOMEM - memory allocation failed
    @retval E2BIG - the source object string will not fit in the target object
    @retval EINVAL - invalid arguments

==============================================================================*/
int VAROBJECT_Copy( VarObject *pDst, VarObject *pSrc )
{
    int result = EINVAL;

    if( ( pSrc != NULL ) &&
        ( pDst != NULL ) )
    {
        if( pSrc->type == VARTYPE_STR )
        {
            result = varobject_CopyString( pDst, pSrc );
        }
        else if ( pSrc->type == VARTYPE_BLOB )
        {
            result = varobject_CopyBlob( pDst, pSrc );
        }
        else
        {
            /* copy primitive type */
            pDst->val = pSrc->val;
            pDst->len = pSrc->len;
            pDst->type = pSrc->type;

            result = EOK;
        }
    }

    return result;
}

/*============================================================================*/
/*  varobject_CopyString                                                      */
/*!
    Copy one string VarObject to another

    The varobject_CopyString function is used to copy one string
    VarObject to another.

    If the destination object already points to a string buffer,
    then that string buffer will be used.
    If the destination object does not have a string buffer, then one
    will be allocated for it.

    @param[in]
        pDst
            pointer to the destination VarObject

    @param[in]
        pSrc
            pointer to the source VarObject

    @retval EOK - the VarObject was copied ok
    @retval ENOMEM - memory allocation failed
    @retval E2BIG - the source object string will not fit in the target object
    @retval EINVAL - invalid arguments

==============================================================================*/
static int varobject_CopyString( VarObject *pDst, VarObject *pSrc )
{
    int result = EINVAL;
    char *pSrcString;
    size_t srclen;

    if ( ( pSrc != NULL ) &&
         ( pDst != NULL ) &&
         ( pSrc != pDst ) )
    {
        /* get the source object length */
        srclen = pSrc->len;

        /* set the destination object type */
        pDst->type = pSrc->type;

        /* get the source string */
        pSrcString = pSrc->val.str;
        if( pSrcString != NULL )
        {
            if( pDst->val.str == NULL )
            {
                /* allocate memory for the target string */
                pDst->val.str = calloc( 1, srclen );
                pDst->len = srclen;
            }
            else
            {
                /* calculate the length of the source string */
                srclen = strlen( pSrcString ) + 1;
            }

            if( pDst->val.str != NULL )
            {
                if( pDst->len >= srclen )
                {
                    /* get source string  */
                    strcpy( pDst->val.str,
                            pSrc->val.str );

                    result = EOK;
                }
                else
                {
                    /* not enough space to store the string */
                    result = E2BIG;
                }
            }
            else
            {
                /* no memory available for the string result */
                result = ENOMEM;
            }
        }
        else
        {
            /* should not see this.  The source object says it is a string
                but it does not have a string pointer */
            result = ENOTSUP;
        }
    }

    return result;
}

/*============================================================================*/
/*  varobject_CopyBlob                                                        */
/*!
    Copy one blob VarObject to another

    The varobject_CopyBlob function is used to copy one blob
    VarObject to another.

    If the destination object already points to a blob buffer,
    then that string buffer will be used.
    If the destination object does not have a blob buffer, then one
    will be allocated for it.

    @param[in]
        pDst
            pointer to the destination VarObject

    @param[in]
        pSrc
            pointer to the source VarObject

    @retval EOK - the VarObject was copied ok
    @retval ENOMEM - memory allocation failed
    @retval E2BIG - the source object blob will not fit in the target object
    @retval EINVAL - invalid arguments

==============================================================================*/
static int varobject_CopyBlob( VarObject *pDst, VarObject *pSrc )
{
    int result = EINVAL;
    char *pSrcData;
    size_t srclen;

    if ( ( pSrc != NULL ) &&
         ( pDst != NULL ) &&
         ( pSrc != pDst ) )
    {
        /* get the source object length */
        srclen = pSrc->len;

        /* set the destination object type */
        pDst->type = pSrc->type;

        /* get the source data */
        pSrcData = pSrc->val.blob;
        if( pSrcData != NULL )
        {
            if( pDst->val.blob == NULL )
            {
                /* allocate memory for the target blob */
                pDst->val.blob = calloc( 1, srclen );
                pDst->len = srclen;
            }

            if( pDst->val.blob != NULL )
            {
                if( pDst->len >= srclen )
                {
                    /* get source blob  */
                    memcpy( pDst->val.blob,
                            pSrc->val.blob,
                            srclen );

                    /* set the destination blob size */
                    pDst->len = srclen;

                    result = EOK;
                }
                else
                {
                    /* not enough space to store the blob */
                    result = E2BIG;
                }
            }
            else
            {
                /* no memory available for the blob result */
                result = ENOMEM;
            }
        }
        else
        {
            /* should not see this.  The source object says it is a blob
                but it does not have a blob pointer */
            result = ENOTSUP;
        }
    }

    return result;
}

/*============================================================================*/
/*  VAROBJECT_ToString                                                        */
/*!
    Convert a VarObject value into a string

    The VAROBJECT_ToString function is used to convert a VarObject value
    into a string depending on the datatype of the variable.
    The caller provides the string buffer to write into.

    @param[in]
        pVarObject
            pointer to the VarObject to convert to a string

    @param[in]
        buf
            pointer to the string buffer to write into

    @param[in]
        len
            length of the string buffer

    @retval EOK - the VarObject was converted ok
    @retval E2BIG - the output buffer was not big enough to contain the output
    @retval EINVAL - invalid arguments
    @retval ENOTSUP - unsupported object type

==============================================================================*/
int VAROBJECT_ToString( VarObject *pVarObject, char *buf, size_t len )
{
    int result = EINVAL;
    int n = 0;

    if ( ( pVarObject != NULL ) &&
         ( buf != NULL ) &&
         ( len > 0 ) )
    {
        result = EOK;

        switch( pVarObject->type )
        {
            case VARTYPE_INT16:
                n = snprintf(buf, len, "%d", pVarObject->val.i );
                break;

            case VARTYPE_UINT16:
                n = snprintf(buf, len, "%d", pVarObject->val.ui );
                break;

            case VARTYPE_INT32:
                n = snprintf(buf, len, "%" PRId32, pVarObject->val.l);
                break;

            case VARTYPE_UINT32:
                n = snprintf(buf, len, "%" PRIu32, pVarObject->val.ul);
                break;

            case VARTYPE_INT64:
                n = snprintf(buf, len, "%" PRId64 , pVarObject->val.ll);
                break;

            case VARTYPE_UINT64:
                n = snprintf(buf, len, "%" PRIu64 , pVarObject->val.ull);
                break;

            case VARTYPE_FLOAT:
                n = snprintf(buf, len, "%f", pVarObject->val.f);
                break;

            case VARTYPE_STR:
                n = snprintf(buf, len, "%s", pVarObject->val.str );
                break;

            case VARTYPE_BLOB:
                n = snprintf(buf, len, "%s", "<object>");
                break;

            default:
                result = ENOTSUP;
                break;
        }

        if ( result == EOK )
        {
            /* check for truncation */
            if ( n > 0 )
            {
                if ( (size_t)n >= len )
                {
                    result = E2BIG;
                }
            }
            else
            {
                result = ENOTSUP;
            }
        }
    }

    return result;
}

/*============================================================================*/
/*  str_to_var                                                                */
/*!
    Convert a string variable into a VarObject

    The str_to_var function converts the specified string
    into a VarObject depending on the conversion type requested.

    The str_to_var function covers a number of use cases.  If the
    VAR_OBJECT_OPTION_COPY option is specified, then memory will be
    allocated to store the specified string and it will be copied into
    the allocated buffer.

    If the pVarObject->len field is set, then the memory will be allocated
    to that specified length, otherwise the length of the string plus one
    byte for the NUL terminator will be used instead.

    @param[in]
        str
            pointer to a NUL terminated string containing the value to convert

    @param[out]
        pVarObject
            pointer to the VarObject to contain the result

    @param[in]
        options
            conversion options which may be OR'd together to
            control the conversion behavior

            VAROBJECT_OPTION_COPY - indicates the string should be copied
                                    to a newly allocated buffer (in the case
                                    of a VARTYPE_STR type)

    @retval EOK - the variable was converted ok
    @retval ENOMEM - memory allocation failed
    @retval E2BIG - string does not fit in the variable
    @retval EINVAL - invalid arguments

==============================================================================*/
static int str_to_var( char *str,
                       VarObject *pVarObject,
                       uint32_t options )
{
    int result = EINVAL;
    size_t n;
    char *pStrCopy;

    if( ( str != NULL ) &&
        ( pVarObject != NULL ) )
    {
        /* store the variable type */
        pVarObject->type = VARTYPE_STR;

        /* get the input string length */
        n = strlen( str );

        if( pVarObject->len == 0 )
        {
            /* length was not specified in the VarObject
                to take the string length and add 1 for
                a NUL terminator */
            pVarObject->len = n + 1;
        }

        if( n < pVarObject->len )
        {
            if( options & VAROBJECT_OPTION_COPY )
            {
                /* allocate memory for the new string */
                pStrCopy = malloc( pVarObject->len );
                if( pStrCopy != NULL )
                {
                    /* copy the source data into the new string */
                    strcpy( pStrCopy, str );
                    pVarObject->val.str = pStrCopy;
                    result = EOK;
                }
                else
                {
                    pVarObject->val.str = NULL;
                    pVarObject->len = 0L;
                    result = ENOMEM;
                }
            }
            else
            {
                /* store a reference to the specified string */
                pVarObject->val.str = str;
                result = EOK;
            }
        }
        else
        {
            /* the specified string will not fit in the specified
                buffer size */
            result = E2BIG;
        }
    }

    return result;
}

/*============================================================================*/
/*  blobstr_to_var                                                            */
/*!
    Convert a blob string variable into a VarObject

    The blobstr_to_var function converts the specified string
    into a VarObject depending on the conversion type requested.

    The str_to_var function covers a number of use cases.  If the
    VAR_OBJECT_OPTION_COPY option is specified, then memory will be
    allocated to store the specified string and it will be copied into
    the allocated buffer.

    If the pVarObject->len field is set, then the memory will be allocated
    to that specified length, otherwise the length of the string plus one
    byte for the NUL terminator will be used instead.

    @param[in]
        str
            pointer to a NUL terminated string containing the value to convert

    @param[out]
        pVarObject
            pointer to the VarObject to contain the result

    @param[in]
        options
            conversion options which may be OR'd together to
            control the conversion behavior

            VAROBJECT_OPTION_COPY - indicates the string should be copied
                                    to a newly allocated buffer (in the case
                                    of a VARTYPE_BLOB type)

    @retval EOK - the variable was converted ok
    @retval ENOMEM - memory allocation failed
    @retval E2BIG - string does not fit in the variable
    @retval EINVAL - invalid arguments

==============================================================================*/
static int blobstr_to_var( char *str,
                           VarObject *pVarObject,
                           uint32_t options )
{
    int result = EINVAL;
    size_t n;
    void *pBlobCopy;

    if( ( str != NULL ) &&
        ( pVarObject != NULL ) )
    {
        /* store the variable type */
        pVarObject->type = VARTYPE_BLOB;

        /* get the input string length */
        n = strlen( str );

        if( pVarObject->len == 0 )
        {
            /* length was not specified in the VarObject
                to take the string length and add 1 for
                a NUL terminator */
            pVarObject->len = n + 1;
        }

        if( n < pVarObject->len )
        {
            if( options & VAROBJECT_OPTION_COPY )
            {
                /* allocate memory for the new string */
                pBlobCopy = malloc( pVarObject->len );
                if( pBlobCopy != NULL )
                {
                    /* copy the source data into the new string */
                    strcpy( (char *)pBlobCopy, str );
                    pVarObject->val.blob = pBlobCopy;
                    result = EOK;
                }
                else
                {
                    pVarObject->val.blob = NULL;
                    pVarObject->len = 0L;
                    result = ENOMEM;
                }
            }
            else
            {
                /* store a reference to the specified string */
                pVarObject->val.blob = (void *)str;
                result = EOK;
            }
        }
        else
        {
            /* the specified string will not fit in the specified
                buffer size */
            result = E2BIG;
        }
    }

    return result;
}

/*============================================================================*/
/*  floatstr_to_var                                                           */
/*!
    Convert a string variable containing a float into a VarObject

    The floatstr_to_var function converts the specified string
    containing a floating point value into a VarObject

    @param[in]
        str
            pointer to a NUL terminated string containing the value to convert

    @param[out]
        pVarObject
            pointer to the VarObject to contain the result

    @param[in]
        options
            unused

    @retval EOK - the variable was converted ok
    @retval ERANGE - the value could not be represented in a float
    @retval EINVAL - invalid arguments

==============================================================================*/
static int floatstr_to_var( char *str,
                            VarObject *pVarObject,
                            uint32_t options )
{
    int result = EINVAL;
    float f;

    /* options currently unused */
    (void)options;

    if( ( str != NULL ) &&
        ( pVarObject != NULL ) )
    {
        f = strtof( str, NULL );
        if( errno != ERANGE )
        {
            pVarObject->type = VARTYPE_FLOAT;
            pVarObject->len = sizeof( float );
            pVarObject->val.f = f;
            result = EOK;
        }
        else
        {
            result = ERANGE;
        }
    }

    return result;
}

/*============================================================================*/
/*  uint32str_to_var                                                          */
/*!
    Convert a string variable containing a uint32 into a VarObject

    The uint32str_to_var function converts the specified string
    containing a 32-bit unsigned integer into a VarObject.

    @param[in]
        str
            pointer to a NUL terminated string containing the value to convert

    @param[out]
        pVarObject
            pointer to the VarObject to contain the result

    @param[in]
        options
            unused

    @retval EOK - the variable was converted ok
    @retval ERANGE - the value could not be represented in a float
    @retval EINVAL - invalid arguments

==============================================================================*/
static int uint32str_to_var( char *str,
                             VarObject *pVarObject,
                             uint32_t options )
{
    int result = EINVAL;
    uint32_t ul;

    /* options currently unused */
    (void)options;

    if( ( str != NULL ) &&
        ( pVarObject != NULL ) )
    {
        if ( check_positive_integer( str ) )
        {
            ul = strtoul( str, NULL, 0 );
            if( errno == ERANGE )
            {
                result = ERANGE;
            }
            else
            {
                pVarObject->len = sizeof( uint32_t );
                pVarObject->type = VARTYPE_UINT32;
                pVarObject->val.ul = ul;
                result = EOK;
            }
        }
        else
        {
            result = ERANGE;
        }
    }

    return result;
}

/*============================================================================*/
/*  int32str_to_var                                                           */
/*!
    Convert a string variable containing an int32 into a VarObject

    The int32str_to_var function converts the specified string
    containing a 32-bit signed integer into a VarObject.

    @param[in]
        str
            pointer to a NUL terminated string containing the value to convert

    @param[out]
        pVarObject
            pointer to the VarObject to contain the result

    @param[in]
        options
            unused

    @retval EOK - the variable was converted ok
    @retval EINVAL - invalid arguments

==============================================================================*/
static int int32str_to_var( char *str,
                            VarObject *pVarObject,
                            uint32_t options )
{
    int result = EINVAL;
    int32_t l;

    /* options currently unused */
    (void)options;

    if( ( str != NULL ) &&
        ( pVarObject != NULL ) )
    {
        l = strtol( str, NULL, 0 );
        if( errno == ERANGE )
        {
            result = ERANGE;
        }
        else
        {
            pVarObject->len = sizeof( int32_t );
            pVarObject->type = VARTYPE_INT32;
            pVarObject->val.l = l;
            result = EOK;
        }
    }

    return result;
}

/*============================================================================*/
/*  uint64str_to_var                                                          */
/*!
    Convert a string variable containing a uint64 into a VarObject

    The uint64str_to_var function converts the specified string
    containing a 64-bit unsigned integer into a VarObject.

    @param[in]
        str
            pointer to a NUL terminated string containing the value to convert

    @param[out]
        pVarObject
            pointer to the VarObject to contain the result

    @param[in]
        options
            unused

    @retval EOK - the variable was converted ok
    @retval ERANGE - the value could not be represented in a float
    @retval EINVAL - invalid arguments

==============================================================================*/
static int uint64str_to_var( char *str,
                             VarObject *pVarObject,
                             uint32_t options )
{
    int result = EINVAL;
    uint64_t ull;

    /* options currently unused */
    (void)options;

    if( ( str != NULL ) &&
        ( pVarObject != NULL ) )
    {
        if ( check_positive_integer( str ) )
        {
            ull = strtoull( str, NULL, 0 );
            if( errno == ERANGE )
            {
                result = ERANGE;
            }
            else
            {
                pVarObject->len = sizeof( uint64_t );
                pVarObject->type = VARTYPE_UINT64;
                pVarObject->val.ull = ull;
                result = EOK;
            }
        }
        else
        {
            result = ERANGE;
        }
    }

    return result;
}

/*============================================================================*/
/*  int64str_to_var                                                           */
/*!
    Convert a string variable containing an int32 into a VarObject

    The int64str_to_var function converts the specified string
    containing a 64-bit signed integer into a VarObject.

    @param[in]
        str
            pointer to a NUL terminated string containing the value to convert

    @param[out]
        pVarObject
            pointer to the VarObject to contain the result

    @param[in]
        options
            unused

    @retval EOK - the variable was converted ok
    @retval EINVAL - invalid arguments

==============================================================================*/
static int int64str_to_var( char *str,
                            VarObject *pVarObject,
                            uint32_t options )
{
    int result = EINVAL;
    int64_t ll;

    /* options currently unused */
    (void)options;

    if( ( str != NULL ) &&
        ( pVarObject != NULL ) )
    {
        ll = strtoll( str, NULL, 0 );
        if( errno == ERANGE )
        {
            result = ERANGE;
        }
        else
        {
            pVarObject->len = sizeof( int64_t );
            pVarObject->type = VARTYPE_INT64;
            pVarObject->val.ll = ll;
            result = EOK;
        }
    }

    return result;
}

/*============================================================================*/
/*  uint16str_to_var                                                          */
/*!
    Convert a string variable containing a uint16 into a VarObject

    The uint16str_to_var function converts the specified string
    containing a 16-bit unsigned integer into a VarObject.

    @param[in]
        str
            pointer to a NUL terminated string containing the value to convert

    @param[out]
        pVarObject
            pointer to the VarObject to contain the result

    @param[in]
        options
            unused

    @retval EOK - the variable was converted ok
    @retval ERANGE - the value could not be represented in a float
    @retval EINVAL - invalid arguments

==============================================================================*/
static int uint16str_to_var( char *str,
                             VarObject *pVarObject,
                             uint32_t options )
{
    int result = EINVAL;
    uint32_t ul;

    /* options currently unused */
    (void)options;

    if( ( str != NULL ) &&
        ( pVarObject != NULL ) )
    {
        if ( check_positive_integer( str ) )
        {
            ul = strtoul( str, NULL, 0 );
            if( errno == ERANGE )
            {
                /* conversion out of range */
                result = ERANGE;
            }
            else if( ul <= 65535 )
            {
                pVarObject->type = VARTYPE_UINT16;
                pVarObject->val.ui = (uint16_t)(ul & 0xFFFF);
                pVarObject->len = sizeof( uint16_t );
                result = EOK;
            }
            else
            {
                result = ERANGE;
            }
        }
        else
        {
            result = ERANGE;
        }
    }

    return result;
}

/*============================================================================*/
/*  int16str_to_var                                                           */
/*!
    Convert a string variable containing a uint16 into a VarObject

    The int16str_to_var function converts the specified string
    containing a 16-bit unsigned integer into a VarObject.

    @param[in]
        str
            pointer to a NUL terminated string containing the value to convert

    @param[out]
        pVarObject
            pointer to the VarObject to contain the result

    @param[in]
        options
            unused

    @retval EOK - the variable was converted ok
    @retval ERANGE - the value could not be represented in an integer
    @retval EINVAL - invalid arguments

==============================================================================*/
static int int16str_to_var( char *str,
                            VarObject *pVarObject,
                            uint32_t options )
{
    int result = EINVAL;
    long l;

    /* options not currently used */
    (void)options;

    if( ( str != NULL ) &&
        ( pVarObject != NULL ) )
    {
        l = strtol( str, NULL, 0 );
        if ( ( errno == ERANGE ) || ( l < -32768 ) || ( l > 32767 ) )
        {
            /* conversion out of range */
            result = ERANGE;
        }
        else
        {
            pVarObject->type = VARTYPE_INT16;
            pVarObject->val.i = (int16_t)l;
            result = EOK;
        }
    }

    return result;
}

/*============================================================================*/
/*  VOBJECT_TypeNameToType                                                    */
/*!
    Convert a type name to its corresponding type

    The VARSERVER_TypeNameToType function converts the specified
    type name into its corresponding enumerated type.

    @param[in]
        typeName
            Name of the type to look up

    @param[out]
        type
            pointer to the output type variable

    @retval EOK - the type name lookup was successful
    @retval EINVAL - invalid arguments
    @retval ENOENT - the specified type name does not exist

==============================================================================*/
int VAROBJECT_TypeNameToType( char *typeName, VarType *type )
{
    int i = 0;
    int result = EINVAL;

    if( ( typeName != NULL ) &&
        ( type != NULL ) )
    {
        /* populate invalid type in case we don't find a match */
        *type = VARTYPE_INVALID;
        result = ENOENT;

        /* iterate through the type names */
        while( varHandlers[i].typeName != NULL )
        {
            /* check if we have a case insensitive type name match */
            if( strcasecmp( varHandlers[i].typeName, typeName ) == 0 )
            {
                *type = i;
                result = EOK;
                break;
            }

            /* select the next type name */
            i++;
        }
    }

    return result;
}

/*============================================================================*/
/*  VAROBJECT_TypeToTypeName                                                  */
/*!
    Get the name of a variable type

    The VAROBJECT_TypeToTypeName function converts the specified
    enumerated type into its corresponding type name.

    @param[in]
        type
            Enumerated type to convert

    @param[out]
        typeName
            pointer to an output buffer to copy the type name

    @param[in]
        len
            length of the output buffer to receive the type name

    @retval EOK - the type name lookup was successful
    @retval EINVAL - invalid arguments
    @retval ENOENT - the specified type name does not exist
    @retval E2BIG - the type name is too long for the output buffer

==============================================================================*/
int VAROBJECT_TypeToTypeName( VarType type, char *typeName, size_t len )
{
    int result = EINVAL;
    size_t typeNameLength;

    if( typeName != NULL )
    {
        if( type < VARTYPE_END_MARKER )
        {
            typeNameLength = strlen( varHandlers[type].typeName );
            if( typeNameLength < len )
            {
                strcpy( typeName, varHandlers[type].typeName );
                result = EOK;
            }
            else
            {
                result = E2BIG;
            }
        }
        else
        {
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  check_positive_integer                                                    */
/*!
    Check that the specified string is a positive integer

    The check_positive_integer function checks all the characters of
    the specified string and confirms that they are all numeric
    or hex digits.

    @param[in]
        str
            pointer to the string to evaluate

    @retval true - the integer is hexadecimal or positive
    @retval false - the integer is not hexadecimal or positive

==============================================================================*/
static bool check_positive_integer( char *str )
{
    char *p = str;
    bool result = false;
    char c;
    size_t count = 0;
    int(*fn)(int) = isdigit;

    if ( p != NULL )
    {
        result = true;

        if( ( p[0] == '0' ) && ( toupper(p[1]) == 'X') )
        {
            fn = isxdigit;
            p = &p[2];
        }

        while ( ( c = *p++ ) != 0 )
        {
            count++;

            if ( !fn(c) )
            {
                result = false;
            }
        }
    }

    return ( count == 0 ) ? false : result;
}

/*! @}
 * end of varobject group */
