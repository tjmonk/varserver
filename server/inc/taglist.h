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

#ifndef TAGLIST_H
#define TAGLIST_H

/*============================================================================
        Includes
============================================================================*/

#include <stdint.h>
#include <varserver/varserver.h>

/*============================================================================
        Public definitions
============================================================================*/

#ifndef VARSERVER_MAX_TAGS
/*! Maximum number of tags */
#define VARSERVER_MAX_TAGS                 ( 256 )
#endif

/*============================================================================
        Public function declarations
============================================================================*/

int TAGLIST_AddNew( char *pTagName, uint16_t *pTagNumber );
int TAGLIST_Parse( char *pTagspec, uint16_t *pTags, size_t len );
int TAGLIST_TagsToString( uint16_t *pTags, size_t len, char *pBuf, size_t n );
int TAGLIST_GetTagNumber( char *pTagName, uint16_t *pTagNumber );
int TAGLIST_GetTagName( uint16_t tagNumber, char *pTagName, size_t size );

#endif
