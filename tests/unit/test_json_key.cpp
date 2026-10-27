// Exact JSON key lookup (round-2 follow-up): the minimal readers used to find
// keys with a raw substring search, so a string *value* or a nested key with
// the same text could be returned instead of the real key.

#include <gtest/gtest.h>

#include <string>

#include "core/json_key.hpp"
#include "ui/automation/automation_json.hpp"

using namespace spectra;

TEST(JsonKey, MatchesOnlyExactKeyTokens)
{
    const std::string json = R"({"max": 1, "x_label": "x", "x": 5})";
    const size_t      pos  = json_find_key(json, "x");
    ASSERT_NE(pos, std::string::npos);
    EXPECT_EQ(json.compare(pos, 5, "\"x\": "), 0);
    EXPECT_EQ(json_find_key(json, "ma"), std::string::npos);
    EXPECT_EQ(json_find_key(json, "missing"), std::string::npos);
}

TEST(JsonKey, StringValueIsNotAKey)
{
    // A lookup of "y" used to land on the value "y" and then read the next
    // ':' (belonging to "x") — returning 1 instead of 2.
    const std::string json = R"({"axis": "y", "x": 1, "y": 2})";
    EXPECT_EQ(json_get_int(json, "y", -1), 2);
}

TEST(JsonKey, EscapedQuotesInsideValuesAreSkipped)
{
    const std::string json = R"({"label": "a \"name\": b", "name": "real"})";
    EXPECT_EQ(json_get_string(json, "name"), "real");
}

TEST(JsonKey, PrefersShallowestKey)
{
    const std::string json = R"({"child": {"name": "inner"}, "name": "outer"})";
    EXPECT_EQ(json_get_string(json, "name"), "outer");
    // A uniquely named nested key is still found.
    const std::string nested = R"({"child": {"leaf": 7}})";
    EXPECT_EQ(json_get_int(nested, "leaf", -1), 7);
}

TEST(JsonKey, WhitespaceBeforeColonIsAccepted)
{
    const std::string json = "{\"x\"  :\n 3}";
    EXPECT_EQ(json_get_int(json, "x", -1), 3);
}
