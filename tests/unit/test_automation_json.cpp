// Tests for the shared automation JSON helpers (src/ui/automation/automation_json.hpp):
// string-escape decoding in json_get_string, escaping in json_escape, and round-trips.

#include <gtest/gtest.h>

#include <string>

#include "ui/automation/automation_json.hpp"

using namespace spectra;

namespace
{

std::string get_label(const std::string& json)
{
    return json_get_string(json, "label");
}

}   // namespace

TEST(AutomationJsonGetString, PlainStringUnchanged)
{
    EXPECT_EQ(get_label(R"({"label":"Signal A"})"), "Signal A");
    EXPECT_EQ(get_label(R"({"label":""})"), "");
}

TEST(AutomationJsonGetString, DecodesEmDashEscape)
{
    // What Python's json.dumps() emits by default (ensure_ascii=True).
    EXPECT_EQ(get_label(R"({"label":"Signal A \u2014 auto palette"})"),
              "Signal A \xE2\x80\x94 auto palette");
}

TEST(AutomationJsonGetString, DecodesGreekAndMicro)
{
    EXPECT_EQ(get_label(R"({"label":"\u0394t"})"), "\xCE\x94t");   // Δt
    EXPECT_EQ(get_label(R"({"label":"\u00b5s"})"), "\xC2\xB5s");   // µs (micro sign)
    EXPECT_EQ(get_label(R"({"label":"\u03BC"})"), "\xCE\xBC");     // μ, upper-case hex
}

TEST(AutomationJsonGetString, DecodesSimpleEscapes)
{
    EXPECT_EQ(get_label(R"({"label":"say \"hi\""})"), "say \"hi\"");
    EXPECT_EQ(get_label(R"({"label":"line1\nline2"})"), "line1\nline2");
    EXPECT_EQ(get_label(R"({"label":"C:\\temp\\x"})"), "C:\\temp\\x");
    EXPECT_EQ(get_label(R"({"label":"a\/b"})"), "a/b");
    EXPECT_EQ(get_label(R"({"label":"\b\f\r\t"})"), "\b\f\r\t");
}

TEST(AutomationJsonGetString, EscapedQuoteDoesNotTerminate)
{
    const std::string json = R"({"label":"x\"y","type":"scatter"})";
    EXPECT_EQ(get_label(json), "x\"y");
    EXPECT_EQ(json_get_string(json, "type"), "scatter");
}

TEST(AutomationJsonGetString, DecodesSurrogatePairToAstralUtf8)
{
    // U+1F600 GRINNING FACE = \uD83D\uDE00 → F0 9F 98 80
    EXPECT_EQ(get_label(R"({"label":"\uD83D\uDE00"})"), "\xF0\x9F\x98\x80");
    // U+1D4B3 MATHEMATICAL SCRIPT CAPITAL X = \ud835\udcb3
    EXPECT_EQ(get_label(R"({"label":"x=\ud835\udcb3"})"), "x=\xF0\x9D\x92\xB3");
}

TEST(AutomationJsonGetString, EscapedEqualsRawUtf8)
{
    const std::string escaped = get_label(R"({"label":"\u0394 \u03bc \u2014 \ud83d\ude00"})");
    const std::string raw     = get_label("{\"label\":\"Δ μ — 😀\"}");
    EXPECT_EQ(escaped, raw);
    EXPECT_EQ(raw, "Δ μ — 😀");
}

TEST(AutomationJsonGetString, LoneSurrogatesBecomeReplacementChar)
{
    const std::string fffd = "\xEF\xBF\xBD";
    EXPECT_EQ(get_label(R"({"label":"a\uD83Db"})"), "a" + fffd + "b");   // lone high
    EXPECT_EQ(get_label(R"({"label":"a\uDE00b"})"), "a" + fffd + "b");   // lone low
    EXPECT_EQ(get_label(R"({"label":"\uDE00\uD83D"})"), fffd + fffd);    // reversed
    // High surrogate followed by a non-surrogate escape: replacement + that char.
    EXPECT_EQ(get_label(R"({"label":"\uD83D\u0041"})"), fffd + "A");
    EXPECT_EQ(get_label(R"({"label":"\uD83D"})"), fffd);   // at end
}

TEST(AutomationJsonGetString, NulEscapeBecomesReplacementChar)
{
    const std::string v = get_label(R"({"label":"a\u0000b"})");
    EXPECT_EQ(v,
              "a\xEF\xBF\xBD"
              "b");
    EXPECT_EQ(v.find('\0'), std::string::npos);
}

TEST(AutomationJsonGetString, MalformedEscapesKeptLiterally)
{
    EXPECT_EQ(get_label(R"({"label":"bad\xescape"})"), R"(bad\xescape)");
    EXPECT_EQ(get_label(R"({"label":"\u12"})"), R"(\u12)");
    EXPECT_EQ(get_label(R"({"label":"\u12G4z"})"), R"(\u12G4z)");
    // Unterminated string: decode what is there, do not crash.
    EXPECT_EQ(get_label(R"({"label":"abc\u00e9)"), "abc\xC3\xA9");
    EXPECT_EQ(get_label("{\"label\":\"abc\\"), "abc\\");
}

TEST(AutomationJsonGetString, NonStringValuesReturnedRaw)
{
    const std::string json = R"({"figure_id":3,"x":-1.5e2,"flag":true})";
    EXPECT_EQ(json_get_string(json, "figure_id"), "3");
    EXPECT_EQ(json_get_uint64(json, "figure_id"), 3u);
    EXPECT_DOUBLE_EQ(json_get_number(json, "x"), -150.0);
    EXPECT_EQ(json_get_string(json, "flag"), "true");
}

TEST(AutomationJsonEscape, EscapesControlCharacters)
{
    EXPECT_EQ(json_escape("a\"b\\c"), R"(a\"b\\c)");
    EXPECT_EQ(json_escape("\n\r\t\b\f"), R"(\n\r\t\b\f)");
    EXPECT_EQ(json_escape(std::string("\x01\x1f", 2)), R"(\u0001\u001f)");
    // Non-ASCII UTF-8 passes through unescaped (valid JSON).
    EXPECT_EQ(json_escape("Δ — 😀"), "Δ — 😀");
}

TEST(AutomationJsonEscape, RoundTripsThroughGetString)
{
    const std::string cases[] = {
        "Signal A — auto palette",
        "quote \" backslash \\ slash /",
        "multi\nline\ttabbed\r\b\f",
        std::string("ctl\x01\x02\x1f", 6),
        "Δt [μs] 😀",
        "",
    };
    for (const auto& original : cases)
    {
        const std::string json = "{\"label\":\"" + json_escape(original) + "\",\"type\":\"line\"}";
        EXPECT_EQ(get_label(json), original) << json;
        EXPECT_EQ(json_get_string(json, "type"), "line") << json;
    }
}

TEST(AutomationJsonGetObject, BracesInsideStringsDoNotEndObject)
{
    const std::string json =
        R"({"method":"add_series","params":{"figure_id":1,"label":"a}b \"}\" {c"},"tail":1})";
    const std::string params = json_get_object(json, "params");
    EXPECT_EQ(params, R"({"figure_id":1,"label":"a}b \"}\" {c"})");
    EXPECT_EQ(get_label(params), "a}b \"}\" {c");
    EXPECT_EQ(json_get_uint64(params, "figure_id"), 1u);
}
