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
 * @brief Template processor
 * @{
 */

/*============================================================================*/
/*!
@file vartemplate.c

    Template processor

    The vartemplate Application processes the supplied template
    and replaces variables in the form $(variable) with their
    values before outputting the result to the specified output
    file

*/
/*============================================================================*/

/*==============================================================================
        Includes
==============================================================================*/

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <varserver/varserver.h>
#include <varserver/vartemplate.h>

/*==============================================================================
        Private definitions
==============================================================================*/

/*==============================================================================
        Private file scoped variables
==============================================================================*/

/*==============================================================================
        Public function declarations
==============================================================================*/
void main( int argc, char **argv );

/*==============================================================================
        Private function declarations
==============================================================================*/
void usage( void );

/*==============================================================================
        Function definitions
==============================================================================*/

/*============================================================================*/
/*  main                                                                      */
/*!
    Main entry point for the vartemplate application

    The main function starts the vartemplate application

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
    char *inputFile = NULL;
    char *outputFile;
    int fd_in;
    int fd_out = STDOUT_FILENO;
    int c;
    int result;
    VARSERVER_HANDLE hVarServer;

    while( ( c = getopt( argc, argv, "o:h" ) ) != -1 )
    {
        switch( c )
        {
            case 'o':
                outputFile = optarg;
                fd_out = open( outputFile, O_CREAT | O_WRONLY, 0600 );
                break;

            case 'h':
                usage();
                break;

            default:
                printf("invalid option: %c\n", c );
                break;
        }
    }

    /* get the name of the input file */
    inputFile = argv[optind];

    if( inputFile != NULL )
    {
        /* get a handle the variable server */
        hVarServer = VARSERVER_Open();

        /* open the template */
        fd_in = open( inputFile, O_RDONLY );
        if( ( fd_in >= 0 ) &&
            ( hVarServer != NULL ) )
        {
            /* render the template to the output */
            result = TEMPLATE_FileToFile( hVarServer, fd_in, fd_out );
        }

        /* close the output file */
        close( fd_out);

        /* close connection to the variable server */
        VARSERVER_Close( hVarServer );
    }
    else
    {
        fprintf( stderr, "No input file specified\n" );
    }
}

/*============================================================================*/
/*  usage                                                                     */
/*!
    Program usage message

    The usage function outputs the program usage message to stdout
    and aborts the application

==============================================================================*/
void usage( void )
{
    printf("usage: print_template [-o output_file] [-h]\n" );
    exit( 0 );
}

/*! @}
 * end of vartemplate group */
