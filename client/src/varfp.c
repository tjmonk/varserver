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
 * @defgroup varfp varfp
 * @brief Create a FILE * backed shared buffer for printing variables into
 * @{
 */

/*============================================================================*/
/*!
@file varfp.c

    Manage a shared buffer which can be printed into from another process

    The VarFP module supports creation and deletion of a shared memory
    buffer which is backed by a file which can be printed into
    from another process, allowing rendering of system variable data
    into a memory buffer.

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
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <varserver/varfp.h>

/*==============================================================================
        Private type declarations
==============================================================================*/

/*! VarFP object mapping a FILE * to a shared memory object */
typedef struct _VarFP
{
    /*! file descriptor */
    int fd;

    /*! memory buffer */
    char *pData;

    /*! shared memory length */
    size_t length;

    /*! client name */
    char clientName[BUFSIZ];

} VarFP;

/*==============================================================================
        Private function declarations
==============================================================================*/

/*==============================================================================
        File scoped variables
==============================================================================*/

/*==============================================================================
        Function definitions
==============================================================================*/

/*============================================================================*/
/*  VARFP_Open                                                                */
/*!

    Create a memory buffer we can print into using a FILE * from another process

    The VARFP_Open function creates a shared memory object which is backed
    by a FILE * using mmap and shm_open which can be printed into by another
    process.

@param[in]
    name
        pointer to a base name for the file

@param[in]
    len
        length of the data buffer to be created

@retval EOK the file backed shared memory was successfully created
@retval ENOMEM memory allocation failed
@retval EBADF cannot create FILE *
@retval ENXIO cannot set length of shared memory block
@retval EINVAL invalid arguments

==============================================================================*/
VarFP *VARFP_Open( char *name, size_t len )
{
    int result = EINVAL;
    pid_t pid;
    VarFP *pVarFP = NULL;
    int rc;

    if ( ( name != NULL ) &&
         ( len > 0 ) )
    {
        /* allocate memory for the VarFP object */
        pVarFP = calloc( 1, sizeof( VarFP ) );
        if( pVarFP != NULL )
        {
            /* get the caller's process identifier */
            pid = getpid();

            /* build a unique name */
            sprintf( pVarFP->clientName, "/%s_%d", name, pid );

            /* open a shared memory file descriptor */
            pVarFP->fd = shm_open( pVarFP->clientName,
                                   O_RDWR | O_CREAT,
                                   S_IRUSR | S_IWUSR );
            if( pVarFP->fd != -1 )
            {
                /* set the size of the shared memory block */
                rc = ftruncate( pVarFP->fd, len );
                if( rc != -1 )
                {
                    /* set the length of the shared memory block */
                    pVarFP->length = len;

                    /* get a pointer to the mapped memory */
                    pVarFP->pData = mmap( NULL,
                                          len,
                                          PROT_READ | PROT_WRITE,
                                          MAP_SHARED,
                                          pVarFP->fd,
                                          0 );
                    if( pVarFP->pData != MAP_FAILED )
                    {
                        result = EOK;
                    }
                }
            }
        }
    }

    if( result != EOK )
    {
        if ( pVarFP != NULL )
        {
            VARFP_Close( pVarFP );
            pVarFP = NULL;
        }
    }

    return pVarFP;
}

/*============================================================================*/
/*  VARFP_Close                                                               */
/*!

    Close and deallocated the FILE * backed shared memory

@param[in]
    pVarFP
        pointer to the VarFP object to close

@retval EOK the shared memory was successfully closed
@retval EINVAL invalid arguments

==============================================================================*/
int VARFP_Close( VarFP *pVarFP )
{
    int result = EINVAL;
    if ( pVarFP != NULL )
    {
        if( pVarFP->fd != -1 )
        {
            close( pVarFP->fd );
            pVarFP->fd = -1;
        }

        /* unmap the shared memory */
        if( pVarFP->pData != NULL )
        {
            munmap( pVarFP->pData, pVarFP->length );
        }

        if( strlen( pVarFP->clientName ) > 0 )
        {
            /* unlink the file */
            shm_unlink( pVarFP->clientName );
        }

        /* clear the memory */
        memset( pVarFP, 0, sizeof( VarFP ) );

        /* free the VarFP object */
        free( pVarFP );

        result = EOK;
    }

    return result;
}

/*============================================================================*/
/*  VARFP_GetFd                                                               */
/*!

    Get the file descriptor associated with the specified VarFP object

@param[in]
    pVarFP
        pointer to the VarFP object to query

@retval file descriptor
@retval -1 if there is no attached file descriptor

==============================================================================*/
int VARFP_GetFd( VarFP *pVarFP )
{
    int fd = -1;

    if( pVarFP != NULL )
    {
        fd = pVarFP->fd;
    }

    return fd;
}

/*============================================================================*/
/*  VARFP_GetData                                                             */
/*!

    Get the data buffer associated with the specified VarFP object

@param[in]
    pVarFP
        pointer to the VarFP object to query

@retval pointer to the data associated with the VarFP object
@retval NULL if there is no associated data

==============================================================================*/
char *VARFP_GetData( VarFP *pVarFP )
{
    char *pData = NULL;

    if ( pVarFP != NULL )
    {
        pData = pVarFP->pData;
    }

    return pData;
}

/*============================================================================*/
/*  VARFP_GetSize                                                             */
/*!

    Get the size of the data buffer associated with the specified VarFP object

@param[in]
    pVarFP
        pointer to the VarFP object to query

@retval size of the data associated with the VarFP object
@retval 0 if there is no associated data

==============================================================================*/
size_t VARFP_GetSize( VarFP *pVarFP )
{
    size_t length = 0;

    if ( pVarFP != NULL )
    {
        length = pVarFP->length;
    }

    return length;
}

/*! @}
 * end of varfp group */
