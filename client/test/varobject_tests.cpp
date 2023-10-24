#include <catch2/catch_test_macros.hpp>

#ifdef __cplusplus
extern "C" {
#endif

#include <varserver/varobject.h>

#ifdef __cplusplus
}
#endif

SCENARIO("varobjects can be created from strings", "[varobject]")
{
    GIVEN("Some string and a destination varobject")
    {
        const char* to_be_converted = "tobeconverted";
        VarObject result{};

        WHEN("We call VAROBJECT_CreateFromString")
        {
            const auto ret = VAROBJECT_CreateFromString(const_cast<char*>(to_be_converted), VarType::VARTYPE_STR, &result, 0);
            THEN("Good things happen")
            {
                REQUIRE(ret == EOK);
                REQUIRE(result.len == 14);
                REQUIRE(result.type == VarType::VARTYPE_STR);
                REQUIRE(strcmp(to_be_converted, result.val.str) == 0);
            }
        }
        WHEN("We call VAROBJECT_CreateFromString with NULL")
        {
            const auto ret = VAROBJECT_CreateFromString(NULL, VarType::VARTYPE_STR, &result, 0);
            THEN("an error is returned")
            {
                // TODO: Check and make sure 'result' wasn't written to
                REQUIRE(ret == EINVAL);
            }
        }
        WHEN("We call VAROBJECT_CreateFromString with an empty string")
        {
            const char* empty_string = "";
            const auto ret = VAROBJECT_CreateFromString(const_cast<char*>(empty_string), VarType::VARTYPE_STR, &result, 0);
            THEN("the object is successfully created")
            {
                REQUIRE(ret == EOK);
                REQUIRE(result.len == 1);
                REQUIRE(result.type == VarType::VARTYPE_STR);
                REQUIRE(*result.val.str == '\0');
            }
        }
    }
}
