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
 * @defgroup vartemplate vartemplate
 * @brief Template rendering engine
 * @{
 */

/*============================================================================*/
/*!
@file vartemplate.c

    Create output data from a template

    The template functions provide a mechanism to generate output to
    a string or a file from an input template containing external
    variable references

*/
/*============================================================================*/


/*==============================================================================
        Includes
==============================================================================*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <varserver/var.h>
#include <varserver/varserver.h>
#include <varserver/vartemplate.h>

/*==============================================================================
        Private definitions
==============================================================================*/

/*==============================================================================
        Type Definitions
==============================================================================*/

/*! track the state of the templating engine across function calls */
typedef struct tState
{
    /*! working output buffer */
    char *output;

    /*! working output buffer length */
    ssize_t outputLength;

    /*! variable name buffer */
    char *varname;

    /*! length of the variable name buffer */
    size_t varnameLength;

    /*! output file descriptor */
    int fd_out;

    /*! current index in the output buffer */
    ssize_t j;

    /*! current index in the variable name buffer */
    size_t k;

    /*! current template engine state */
    int state;

    /*! number of substitutions */
    int numberOfSubstitutions;

    /*! number of substitution failures */
    int numberOfSubstitutionFailures;

} TState;

/*==============================================================================
        Private function declarations
==============================================================================*/

static int ProcessInputChunk( VARSERVER_HANDLE hVarServer,
                              TState *pTState,
                              char *input,
                              size_t len );

static int SubstituteVariable( VARSERVER_HANDLE hVarServer, TState *pTState );

static int FlushOutputBuffer( TState *pTState );

static int CheckStartDirective( TState *pState, char c );

static int CheckStartVariable( TState *pState, char c );

static int CheckEndVariable( VARSERVER_HANDLE hVarServer,
                            TState *pTState,
                            char c );

static int CheckOutputBuffer( TState *pTState );

/*==============================================================================
        Function definitions
==============================================================================*/

/*============================================================================*/
/*  TEMPLATE_FileToFile                                                       */
/*!
    Create an output file from an input file containing a rendering template

    The TEMPLATE_FileToFile function reads a template from an input file
    and generates output to an output file by rendering the template
    with variable references replaced with their values.

    @param[in]
        hVarServer
            handle to the Variable Server to create variables for

    @param[in]
        template
            open file handle of the template file to render

    @param[in]
        output
            open file handle of the output file

    @retval EOK - output generation was successful
    @retval ENOTSUP - one or more variable subsititions could not be performed
    @retval EINVAL - invalid arguments

==============================================================================*/
int TEMPLATE_FileToFile( VARSERVER_HANDLE hVarServer,
                         int fd_in,
                         int fd_out )
{
    int result = EINVAL;
    char buf[BUFSIZ];
    char outbuf[BUFSIZ];
    char varname[BUFSIZ];
    bool done = false;
    TState state;
    int n = 0;
    int rc;

    /* initialize the template engine state */
    memset( &state, 0, sizeof(TState) );

    state.output = outbuf;
    state.outputLength = sizeof( outbuf );
    state.varname = varname;
    state.varnameLength = sizeof( varname );
    state.fd_out = fd_out;

    /* assume we are ok, until we are not */
    result = EOK;

    while( !done )
    {
        /* read a chunk of the input buffer */
        n = read( fd_in, buf, BUFSIZ );
        if( n < BUFSIZ )
        {
            done = true;
        }

        if( n > 0 )
        {
            /* process the input buffer chunk to the output */
            rc = ProcessInputChunk( hVarServer, &state, buf, n );
            if( rc != EOK )
            {
                result = rc;
            }
        }

        /* flush the output buffer to the output file descriptor */
        FlushOutputBuffer( &state );
    }

    return result;
}

/*============================================================================*/
/*  TEMPLATE_StrToFile                                                        */
/*!
    Process an input string and expand any variables found

    The TEMPLATE_StrToFile function processes a buffer of input
    searching for any variables in the form ${variable}

    The input string will be written to the output file with
    any variables replaced with their values.

    @param[in]
        hVarServer
            handle to the Variable Server to get the variable values

    @param[in]
        input
            pointer to an input buffer

    @param[in]
        fd
            output file descriptor to write to

    @retval EOK - output generation was successful
    @retval EINVAL - invalid arguments
    @retval E2BIG - the output buffer size was exceeded
    @retval ENOTSUP - one or more substitutions failed

==============================================================================*/
int TEMPLATE_StrToFile( VARSERVER_HANDLE hVarServer,
                        char *input,
                        int fd )
{
    int result = EINVAL;
    char outbuf[BUFSIZ];
    char varname[BUFSIZ];
    int n = 0;
    TState state;

    if ( ( hVarServer != NULL ) &&
         ( input != NULL ) &&
         ( fd > 0 ) )
    {
        /* initialize the translation engine state */
        memset( &state, 0, sizeof( TState ) );

        state.output = outbuf;
        state.outputLength = sizeof( outbuf );
        state.varname = varname;
        state.varnameLength = sizeof( varname );
        state.fd_out = fd;

        /* get the length of the input buffer */
        n = strlen( input );

        /* process the input string and perform variable substitutions */
        result = ProcessInputChunk( hVarServer, &state, input, n );

        /* Flush the output buffer */
        FlushOutputBuffer( &state );
    }

    return result;
}

/*==============================================================================
        Private Function definitions
==============================================================================*/

/*============================================================================*/
/*  ProcessInputChunk                                                         */
/*!
    Process a chunk of an input string and update the translation engine output

    The ProcessInputChunk function processes a buffer of data and updates
    the translation engine output.  This buffer may be one of a sequence of
    buffers.  The translation engine state between calls is managed with the
    TState object which keeps track of the input, output, and variable name
    buffers.

    If the working output buffer space is exceeded, it is flushed to the
    output

    @param[in]
        hVarServer
            handle to the Variable Server to get the variable values

    @param[in]
        pTState
            pointer to an initialized template engine state which contains
            the status of the current template rendering activity

    @param[in]
        input
            pointer to an input buffer to process

    @param[in]
        len
            lengh of the input buffer to process

    @retval EOK - output generation was successful
    @retval EINVAL - invalid arguments
    @retval ENOTSUP - one or more substitutions failed

==============================================================================*/
static int ProcessInputChunk( VARSERVER_HANDLE hVarServer,
                              TState *pTState,
                              char *input,
                              size_t len )
{
    int result = EINVAL;
    size_t i;
    char c;
    int rc;

    if ( ( hVarServer != NULL ) &&
         ( pTState != NULL ) &&
         ( input != NULL ) &&
         ( len > 0 ) )
    {
        /* assume we are ok, until we are not */
        result = EOK;

        for ( i = 0; i < len; i++ )
        {
            /* get the input character */
            c = input[i];

            /* process translation engine state machine */
            switch( pTState->state )
            {
                case 0:
                default:
                    /* look for the start of a directive */
                    rc = CheckStartDirective( pTState, c );
                    break;

                case 1:
                    /* look for the start of a variable */
                    rc = CheckStartVariable( pTState, c );
                    break;

                case 2:
                    rc = CheckEndVariable( hVarServer, pTState, c );
                    break;
            }

            if( rc == ENOENT )
            {
                /* ignore ENOENT, since that just means we are not
                 * transitioning states, and that is ok */
                rc = EOK;
            }

            /* catch any failures */
            if( rc != EOK )
            {
                result = rc;
            }

            /* check if we have reached the end of our working buffer */
            CheckOutputBuffer( pTState );
        }
    }

    return result;
}

/*============================================================================*/
/*  CheckOutputBuffer                                                         */
/*!
    Check if we have reached the end of the output buffer

    The CheckOutputBuffer function checks to see if the working output buffer
    is full, and if it is, it is flushed to the output stream and cleared
    ready for more output data.

    @param[in]
        pTState
            pointer to an initialized template engine state which contains
            the output working buffer

    @retval EOK - output buffer was successfully checked
    @retval EINVAL - invalid arguments

==============================================================================*/
static int CheckOutputBuffer( TState *pTState )
{
    int result = EINVAL;

    if( pTState != NULL )
    {
        /* indicate success */
        result = EOK;

        /* check if we have reached the end of our working buffer */
        if( pTState->j >= pTState->outputLength-1 )
        {
            result = FlushOutputBuffer( pTState );
        }

    }

    return result;
}

/*============================================================================*/
/*  CheckStartDirective                                                       */
/*!
    Check if we are processing the start of a variable substitution directive

    The CheckStartDirective function checks the input character
    to see if it is a '$' symbol which could indicate the start of
    a variable substitution directive.

    If the '$' symbol is found, the template engine state machine is updated
    to look for the next character in the variable substitution directive: '{'.

    If the '$' symbol is not found, the character is a regular text character
    and is copied to the output buffer.

    @param[in]
        pTState
            pointer to an initialized template engine state which contains
            the output working buffer

    @param[in]
        c
            character to be processed from the input stream

    @retval EOK - we have possibly found the start of a directive
    @retval EINVAL - invalid arguments
    @retval ENOENT - normal character copied to the output stream buffer

==============================================================================*/
static int CheckStartDirective( TState *pTState, char c )
{
    int result = EINVAL;

    if( pTState != NULL )
    {
        /* check for $ */
        if( c == '$' )
        {
            /* set next state to look for '{' */
            pTState->state = 1;
            result = EOK;
        }
        else
        {
            /* copy the input data to the output buffer */
            pTState->output[pTState->j++] = c;
            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  CheckStartVariable                                                        */
/*!
    Check if we are processing the start of a variable substitution directive

    The CheckStartVariable function checks the input character
    to see if it is a '{' symbol which indicates the start of
    a variable substitution directive.

    If the '{' symbol is found, the template engine state machine is updated
    to begin collecting the variable name characters

    If the '{' symbol is not found, the character is a regular text character
    and is copied to the output buffer, prefixed with the '$' character which
    was encountered in the previous iteration.

    @param[in]
        pTState
            pointer to an initialized template engine state which contains
            the output working buffer

    @param[in]
        c
            character to be processed from the input stream

    @retval EOK - we have possibly found the start of a directive
    @retval EINVAL - invalid arguments
    @retval ENOENT - normal character copied to the output stream buffer

==============================================================================*/
static int CheckStartVariable( TState *pTState, char c )
{
    int result = EINVAL;

    if( pTState != NULL )
    {
        /* check for start of variable substitution directive */
        if( c == '{' )
        {
            /* found variable substitution */
            pTState->state = 2;

            /* flush the output buffer */
            FlushOutputBuffer( pTState );

            result = EOK;
        }
        else
        {
            /* append the $ to the output buffer since it was
             * not part of a variable substitution directive ${ } */
            pTState->output[pTState->j++] = '$';

            /* check output buffer overflow */
            CheckOutputBuffer( pTState );

            /* append the character to the output buffer since it
             * was not part of the variable substitution directive ${ } */
            pTState->output[pTState->j++] = c;

            result = ENOENT;
        }
    }

    return result;
}


/*============================================================================*/
/*  CheckEndVariable                                                          */
/*!
    Check if we are processing the end of a variable substitution directive

    The CheckEndVariable function checks the input character
    to see if it is a '}' symbol which indicates the end of
    a variable substitution directive.

    If the '}' symbol is found, or the variable name buffer size is exceeded,
    the variable substitution is performed via SubstituteVariable function,
    and the template engine state is reset back to 0 to look for the
    next variable substitution directive.

    If the '}' symbol is not found, and the variable name buffer size is
    not exceeded, the character is part of the variable name, and is appended
    to the variable name buffer.

    @param[in]
        pTState
            pointer to an initialized template engine state which contains
            the variable name buffer

    @param[in]
        c
            character to be processed from the input stream

    @retval EOK - the variable substitution was successful
    @retval EINVAL - invalid arguments
    @retval ENOENT - normal character which is part of the variable name string
    @retval ENOTSUP - Variable was not found and substitution was not done

==============================================================================*/
static int CheckEndVariable( VARSERVER_HANDLE hVarServer,
                            TState *pTState,
                            char c )
{
    int result = EINVAL;

    if ( ( pTState != NULL ) &&
         ( pTState->varname != NULL ) &&
         ( hVarServer != NULL ) )
    {
        /* check if we need to perform variable substitution */
        if( ( c == '}' ) ||
            (  pTState->k == ( pTState->varnameLength - 2 ) ) )
        {
            /* assume everything is going to be ok, until it isn't */
            result = EOK;

            /* perform variable substitution */
            if( SubstituteVariable( hVarServer, pTState ) != EOK )
            {
                result = ENOTSUP;
            }

            /* reset the data processing state to looking for '$' */
            pTState->state = 0;
        }
        else
        {
            /* keep building the variable name */
            pTState->varname[pTState->k++] = c;

            result = ENOENT;
        }
    }

    return result;
}

/*============================================================================*/
/*  SubstituteVariable                                                        */
/*!
    Substitute a variable name with its value and write to the output stream

    The SubstituteVariable function gets the name of the variable to
    substitute from the Template State, gets a handle to the varaible
    from the variable server, and then outputs the variable's value
    to the output stream specified in the Template State object.

    The variable name buffer is cleared ready for the next variable to
    be processed.

    @param[in]
        hVarServer
            handle to the Variable Server to get the variable values

    @param[in]
        pTState
            pointer to an initialized template engine state which contains
            the name of the variable and the output stream to write it to

    @retval EOK - output generation was successful
    @retval EINVAL - invalid arguments
    @retval ENOTSUP - one or more substitutions failed

==============================================================================*/
static int SubstituteVariable( VARSERVER_HANDLE hVarServer, TState *pTState )
{
    int result = EINVAL;
    char *varname;
    VAR_HANDLE hVar;

    if ( ( hVarServer != NULL ) &&
         ( pTState != NULL ) &&
         ( pTState->varname != NULL ) )
    {
        varname = pTState->varname;

        /* NUL terminate the variable name */
        varname[pTState->k] = 0;

        /* get a handle to the variable to be substituted */
        hVar = VAR_FindByName( hVarServer, varname );
        if( hVar != VAR_INVALID )
        {
            /* render the variable value to the output stream */
            VAR_Print( hVarServer, hVar, pTState->fd_out );

            /* increment the number of substitutions */
            pTState->numberOfSubstitutions++;

            /* indicate success */
            result = EOK;
        }
        else
        {
            /* track the number of substitution failures */
            pTState->numberOfSubstitutionFailures++;

            /* could not substitute the variable */
            result = ENOTSUP;
        }

        /* clear the variable name buffer */
        memset( varname, 0, pTState->k );
        pTState->k = 0;
    }

    return result;
}

/*============================================================================*/
/*  FlushOutputBuffer                                                         */
/*!
    Flush the output buffer to the output stream

    The FlushOutputBuffer function writes the current contents of the
    templating engine output buffer to the output stream, flushes
    the output stream to make sure all the data is written to the
    output device, and resets the output buffer pointer to the beginning
    of the output buffer ready for the next output operation.

    @param[in]
        pTState
            pointer to an initialized template engine state which contains
            references to the output buffer, and the output stream

    @retval EOK - output generation was successful
    @retval EINVAL - invalid arguments

==============================================================================*/
static int FlushOutputBuffer( TState *pTState )
{
    int result = EINVAL;
    ssize_t n;

    if ( ( pTState != NULL ) &&
         ( pTState->output != NULL ) )
    {
        if( pTState->j != 0 )
        {
            /* indicate success */
            result = EOK;

            /* output the output buffer */
            n = write( pTState->fd_out, pTState->output, pTState->j );
            if ( n != pTState->j )
            {
                result = EIO;
            }

            /* reset the output buffer */
            memset( pTState->output, 0, pTState->j );

            /* reset the pointer to the beginning of the output buffer */
            pTState->j = 0;
        }
    }

    return result;
}

/*! @}
 * end of vartemplate group */
