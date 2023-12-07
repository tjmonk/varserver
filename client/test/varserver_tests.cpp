#include <catch2/catch_test_macros.hpp>

#ifdef __cplusplus
extern "C" {
#endif

#include <varserver/varserver.h>

#ifdef __cplusplus
}
#endif

class VarserverFixture {
  protected:
   VARSERVER_HANDLE hVarServer;
  public:
   VarserverFixture() : hVarServer{} {
      hVarServer = VARSERVER_Open();
   }
  protected:
 };

// VARSERVER_HANDLE VARSERVER_Open( void );
TEST_CASE("VARSERVER_Open", "[varserver]")
{
    VARSERVER_HANDLE hVarServer = VARSERVER_Open();
    REQUIRE( hVarServer != NULL );
    VARSERVER_Close( hVarServer );
}

// VARSERVER_HANDLE VARSERVER_OpenExt( size_t workbufsize );
TEST_CASE("VARSERVER_OpenExt", "[varserver]")
{
    VARSERVER_HANDLE hVarServer = VARSERVER_OpenExt( 1024 );
    REQUIRE( hVarServer != NULL );
    VARSERVER_Close( hVarServer );
 
    hVarServer = VARSERVER_OpenExt( 0 );
    REQUIRE( hVarServer != NULL );
    VARSERVER_Close( hVarServer );
}

// int VARSERVER_UpdateUser( VARSERVER_HANDLE hVarServer );
// int VARSERVER_SetGroup( void );

// int VARSERVER_Debug( VARSERVER_HANDLE hVarServer, int debug );
// int VARSERVER_GetWorkingBuffer( VARSERVER_HANDLE hVarServer,
//                                 char **pBuf,
//                                 size_t *pLen );
// int VARSERVER_Close( VARSERVER_HANDLE hVarServer );
// int VARSERVER_Test( VARSERVER_HANDLE hVarServer );
// int VARSERVER_CreateVar( VARSERVER_HANDLE hVarServer,
//                          VarInfo *pVarInfo );
// int VARSERVER_WaitSignal( int *sigval );

// /* Flag functions */
// int VARSERVER_StrToFlags( char *flagsString,
//                           uint32_t *flags );

// int VARSERVER_FlagsToStr( uint32_t flags,
//                           char *flagsString,
//                           size_t len );

// /* Permission functions */
// int VARSERVER_ParsePermissionSpec( char *permissionSpec,
//                                    gid_t *permissions,
//                                    size_t *len );

// /* type functions */
// int VARSERVER_TypeNameToType( char *typeName, VarType *type );
// int VARSERVER_TypeToTypeName( VarType type, char *typeName, size_t len );

// /* data value functions */
// int VARSERVER_ParseValueString( VarObject *var, char *valueString );

// /* variable functions */
// VAR_HANDLE VAR_FindByName( VARSERVER_HANDLE hVarServer, char *pName );

// int VAR_GetLength( VARSERVER_HANDLE hVarServer,
//                    VAR_HANDLE hVar,
//                    size_t *len );

// int VAR_GetFlags( VARSERVER_HANDLE hVarServer,
//                   VAR_HANDLE hVar,
//                   VarFlags *flags );

// int VAR_GetInfo( VARSERVER_HANDLE hVarServer,
//                  VAR_HANDLE hVar,
//                  VarInfo *pVarInfo );

// int VAR_Print( VARSERVER_HANDLE hVarServer,
//                VAR_HANDLE hVar,
//                int fd );

// int VAR_Get( VARSERVER_HANDLE hVarServer,
//              VAR_HANDLE hVar,
//              VarObject *pVarObject );

// int VAR_GetStrByName( VARSERVER_HANDLE hVarServer,
//                       char *name,
//                       char *buf,
//                       size_t len );

// int VAR_GetBlobByName( VARSERVER_HANDLE hVarServer,
//                        char *name,
//                        void *buf,
//                        size_t len );

// int VAR_GetType( VARSERVER_HANDLE hVarServer,
//                  VAR_HANDLE hVar,
//                  VarType *pVarType );

// int VAR_GetName( VARSERVER_HANDLE hVarServer,
//                  VAR_HANDLE hVar,
//                  char *buf,
//                  size_t len );

// int dumpmem( uint8_t *p, size_t n);

// int VAR_Set( VARSERVER_HANDLE hVarServer,
//              VAR_HANDLE hVar,
//              VarObject *pVarObject );

// int VAR_SetStr( VARSERVER_HANDLE hVarServer,
//                 VAR_HANDLE hVar,
//                 VarType type,
//                 char *str );

// int VAR_SetNameValue( VARSERVER_HANDLE hVarServer,
//                       char *name,
//                       char *value );

// int VARSERVER_WaitSignal( int *sigval );

// sigset_t VARSERVER_SigMask( void );

// int VAR_Notify( VARSERVER_HANDLE hVarServer,
//                 VAR_HANDLE hVar,
//                 NotificationType notificationType );

// int VAR_GetValidationRequest( VARSERVER_HANDLE hVarServer,
//                               uint32_t id,
//                               VAR_HANDLE *hVar,
//                               VarObject *pVarObject );

// int VAR_SendValidationResponse( VARSERVER_HANDLE hVarServer,
//                                 uint32_t id,
//                                 int response  );

// int VAR_OpenPrintSession( VARSERVER_HANDLE hVarServer,
//                          uint32_t id,
//                          VAR_HANDLE *hVar,
//                          int *fd );

// int VAR_ClosePrintSession( VARSERVER_HANDLE hVarServer,
//                            uint32_t id,
//                            int fd );

// int VAR_GetFirst( VARSERVER_HANDLE hVarServer,
//                   VarQuery *query,
//                   VarObject *obj );
// int VAR_GetNext( VARSERVER_HANDLE hVarServer,
//                  VarQuery *query,
//                  VarObject *obj );

// int VARSERVER_CreateClientQueue( VARSERVER_HANDLE hVarServer,
//                                  long queuelen,
//                                  long msgsize );

// int VAR_GetFromQueue( VARSERVER_HANDLE hVarServer,
//                       VarNotification *pVarNotification,
//                       char *buf,
//                       size_t len );

// int VARSERVER_Signalfd( void );

// int VARSERVER_WaitSignalfd( int fd, int32_t *sigval );