#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#ifdef __cplusplus
extern "C" {
#endif

#include <varserver/varobject.h>

#ifdef __cplusplus
}
#endif

SCENARIO("VAROBJECT_CreateFromString works with multiple inputs", "[varobject]")
{
    GIVEN("Some string and a destination varobject")
    {
        VarObject result{};

        const auto to_be_converted = GENERATE("tobeconverted", "\xfeunprintable", "a moderately long string with some spaces in it", "");
        WHEN("We call VAROBJECT_CreateFromString with a string")
        {
            const auto ret = VAROBJECT_CreateFromString(const_cast<char*>(to_be_converted), VarType::VARTYPE_STR, &result, 0);
            THEN("Good things happen")
            {
                REQUIRE(ret == EOK);
                REQUIRE(result.len == strlen(to_be_converted) + 1);
                REQUIRE(result.type == VarType::VARTYPE_STR);
                REQUIRE(strcmp(to_be_converted, result.val.str) == 0);
            }
        }
        WHEN("We call VAROBJECT_CreateFromString with NULL")
        {
            const auto ret = VAROBJECT_CreateFromString(NULL, VarType::VARTYPE_STR, &result, 0);
            THEN("an error is returned")
            {
                // Maybe check and make sure 'result' wasn't written to as well
                REQUIRE(ret == EINVAL);
            }
        }
    }
}

/* TODO: Implement tests
int VAROBJECT_ValueFromString( char *str,
                               VarObject *pVarObject,
                               uint32_t options );
*/

/* TODO: Implement tests
int VAROBJECT_Copy( VarObject *pDst, VarObject *pSrc );
*/


SCENARIO("VAROBJECT_TypeNameToType works with multiple inputs", "[varobject]")
{
    GIVEN("A valid typeName")
    {
        WHEN("We call VAROBJECT_TypeNameToType with a valid typeName")
        {
            // call VAROBJECT_TypeNameToType with a valid typeName
            THEN("the call returns EOK and the returned type enum is what we expect")
            {
                VarType type;
                REQUIRE(VAROBJECT_TypeNameToType(const_cast<char*>("invalid"), &type) == EOK);
                REQUIRE(type == VarType::VARTYPE_INVALID);
                
                REQUIRE(VAROBJECT_TypeNameToType(const_cast<char*>("uint16"), &type) == EOK);
                REQUIRE(type == VarType::VARTYPE_UINT16);
                
                REQUIRE(VAROBJECT_TypeNameToType(const_cast<char*>("int16"), &type) == EOK);
                REQUIRE(type == VarType::VARTYPE_INT16);

                REQUIRE(VAROBJECT_TypeNameToType(const_cast<char*>("uint32"), &type) == EOK);
                REQUIRE(type == VarType::VARTYPE_UINT32);

                REQUIRE(VAROBJECT_TypeNameToType(const_cast<char*>("int32"), &type) == EOK);
                REQUIRE(type == VarType::VARTYPE_INT32);

                REQUIRE(VAROBJECT_TypeNameToType(const_cast<char*>("uint64"), &type) == EOK);
                REQUIRE(type == VarType::VARTYPE_UINT64);

                REQUIRE(VAROBJECT_TypeNameToType(const_cast<char*>("int64"), &type) == EOK);
                REQUIRE(type == VarType::VARTYPE_INT64);

                REQUIRE(VAROBJECT_TypeNameToType(const_cast<char*>("float"), &type) == EOK);
                REQUIRE(type == VarType::VARTYPE_FLOAT);

                REQUIRE(VAROBJECT_TypeNameToType(const_cast<char*>("str"), &type) == EOK);
                REQUIRE(type == VarType::VARTYPE_STR);

                REQUIRE(VAROBJECT_TypeNameToType(const_cast<char*>("blob"), &type) == EOK);
                REQUIRE(type == VarType::VARTYPE_BLOB);
            }
        }

        WHEN("We call VAROBJECT_TypeNameToType with an invalid typeName")
        {
            // call VAROBJECT_TypeNameToType with an invalid typeName
            THEN("the call returns ENOENT")
            {
                VarType type{VarType::VARTYPE_INT32};
                REQUIRE(VAROBJECT_TypeNameToType(const_cast<char*>("unsupported"), &type) == ENOENT);
                REQUIRE(type == VarType::VARTYPE_INVALID);
            }
        }

        WHEN("We call VAROBJECT_TypeNameToType null arguments")
        {
            THEN("the call returns EINVAL")
            {
                VarType type{VarType::VARTYPE_INT32};
                REQUIRE(VAROBJECT_TypeNameToType(reinterpret_cast<char*>(0), &type) == EINVAL);
                REQUIRE(type == VarType::VARTYPE_INT32);


                REQUIRE(VAROBJECT_TypeNameToType(const_cast<char*>("str"), reinterpret_cast<VarType*>(0)) == EINVAL);
            }
        }
    }
}

SCENARIO("VAROBJECT_TypeToTypeName works with multiple inputs", "[varobject]")
{
    GIVEN("A valid type enum")
    {
        WHEN("We call VAROBJECT_TypeToTypeName with a valid type enum")
        {
            THEN("the call returns EOK and the returned typeName is what we expect")
            {
                char typeName[32];
                REQUIRE(VAROBJECT_TypeToTypeName(VarType::VARTYPE_INVALID, typeName, sizeof(typeName)) == EOK);
                REQUIRE(strcmp(typeName, "invalid") == 0);

                REQUIRE(VAROBJECT_TypeToTypeName(VarType::VARTYPE_UINT16, typeName, sizeof(typeName)) == EOK);
                REQUIRE(strcmp(typeName, "uint16") == 0);

                REQUIRE(VAROBJECT_TypeToTypeName(VarType::VARTYPE_INT16, typeName, sizeof(typeName)) == EOK);
                REQUIRE(strcmp(typeName, "int16") == 0);

                REQUIRE(VAROBJECT_TypeToTypeName(VarType::VARTYPE_UINT32, typeName, sizeof(typeName)) == EOK);
                REQUIRE(strcmp(typeName, "uint32") == 0);

                REQUIRE(VAROBJECT_TypeToTypeName(VarType::VARTYPE_INT32, typeName, sizeof(typeName)) == EOK);
                REQUIRE(strcmp(typeName, "int32") == 0);

                REQUIRE(VAROBJECT_TypeToTypeName(VarType::VARTYPE_UINT64, typeName, sizeof(typeName)) == EOK);
                REQUIRE(strcmp(typeName, "uint64") == 0);

                REQUIRE(VAROBJECT_TypeToTypeName(VarType::VARTYPE_INT64, typeName, sizeof(typeName)) == EOK);
                REQUIRE(strcmp(typeName, "int64") == 0);

                REQUIRE(VAROBJECT_TypeToTypeName(VarType::VARTYPE_FLOAT, typeName, sizeof(typeName)) == EOK);
                REQUIRE(strcmp(typeName, "float") == 0);

                REQUIRE(VAROBJECT_TypeToTypeName(VarType::VARTYPE_STR, typeName, sizeof(typeName)) == EOK);
                REQUIRE(strcmp(typeName, "str") == 0);

                REQUIRE(VAROBJECT_TypeToTypeName(VarType::VARTYPE_BLOB, typeName, sizeof(typeName)) == EOK);
                REQUIRE(strcmp(typeName, "blob") == 0);
            }
        }

        WHEN("We call VAROBJECT_TypeToTypeName with an invalid type enum")
        {
            THEN("the call returns EINVAL")
            {
                char typeName[32];
                REQUIRE(VAROBJECT_TypeToTypeName(VarType::VARTYPE_END_MARKER, typeName, sizeof(typeName)) == EINVAL);
            }
        }

        WHEN("We call VAROBJECT_TypeToTypeName with a too-small buffer")
        {
            THEN("the call returns E2BIG")
            {
                char typeName[32];
                REQUIRE(VAROBJECT_TypeToTypeName(VarType::VARTYPE_FLOAT, typeName, 0) == E2BIG);
            }
        }

        WHEN("We call VAROBJECT_TypeToTypeName with null buffer")
        {
            THEN("the call returns INVAL")
            {
                REQUIRE(VAROBJECT_TypeToTypeName(VarType::VARTYPE_FLOAT, nullptr, 0) == EINVAL);
            }
        }
    }
}

/* TODO: Implement tests
int VAROBJECT_ToString( VarObject *pVarObject, char *buf, size_t len );
*/
