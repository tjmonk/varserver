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

/*==========================================================================*/
/*!
@file varobject.h

    Variable Object Management Header

    The Variable Object Management Header contains all of the public
    definitions and APIs for manipulating the VarObjects data type

*/
/*==========================================================================*/

#ifndef VAROBJECT_H
#define VAROBJECT_H

/*============================================================================
        Includes
============================================================================*/

#include <stdint.h>

/*============================================================================
        Definitions
============================================================================*/

#ifndef EOK
/*! Success response */
#define EOK 0
#endif

/*! No options */
#define VAROBJECT_OPTION_NONE      ( 0 )

/*! Make a copy of the string when constructing a VarObject */
#define VAROBJECT_OPTION_COPY      ( 1 << 0 )

/*============================================================================
        Type Definitions
============================================================================*/

/*! Variable type enumeration */
typedef enum _VarType
{
    /*! invalid variable type */
    VARTYPE_INVALID = 0,

    /*! 16-bit unsigned integer */
    VARTYPE_UINT16,

    /*! 16-bit signed integer */
    VARTYPE_INT16,

    /*! 32-bit unsigned integer */
    VARTYPE_UINT32,

    /*! 32-bit signed integer */
    VARTYPE_INT32,

    /*! 64-bit unsigned integer */
    VARTYPE_UINT64,

    /*! 64-bit signed integer */
    VARTYPE_INT64,

    /*! IEEE754 Floating Point Number */
    VARTYPE_FLOAT,

    /*! NUL terminated character string */
    VARTYPE_STR,

    /*! Blob storage type */
    VARTYPE_BLOB,

    /*! end marker for the type enumeration */
    VARTYPE_END_MARKER

} VarType;

/*! variable data union */
typedef union _VarData
{
    /*! unsigned 16-bit integer */
    uint16_t ui;

    /*! signed 16-bit integer */
    int16_t i;

    /*! unsigned 32-bit integer */
    uint32_t ul;

    /*! signed 32-bit integer */
    int32_t l;

    /*! unsigned 64-bit integer */
    uint64_t ull;

    /*! signed 64-bit-integer */
    int64_t ll;

    /*! IEEE-754 floating point number */
    float f;

    /*! pointer to a NUL terminated string */
    char *str;

    /*! void pointer to blob data */
    void *blob;

} VarData;

/*! A variable object */
typedef struct _VarObject
{
    /*! variable type */
    VarType type;

    /*! variable length */
    size_t len;

    /*! variable value */
    VarData val;

} VarObject;

/*============================================================================
        Public Function Declarations
============================================================================*/

int VAROBJECT_CreateFromString( char *str,
                                VarType type,
                                VarObject *pVarObject,
                                uint32_t options );

int VAROBJECT_ValueFromString( char *str,
                               VarObject *pVarObject,
                               uint32_t options );

int VAROBJECT_Copy( VarObject *pDst, VarObject *pSrc );

int VAROBJECT_TypeNameToType( char *typeName, VarType *type );

int VAROBJECT_TypeToTypeName( VarType type, char *typeName, size_t len );

int VAROBJECT_ToString( VarObject *pVarObject, char *buf, size_t len );

#endif
