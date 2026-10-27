// Tests for the shared automation JSON helpers (src/ui/automation/automation_json.hpp):
// string-escape decoding in json_get_string, escaping in json_escape, and round-trips.

#include <gtest/gtest.h>

#include <string>

#include "ui/automation/automation_json.hpp"

// Raw string literals are kept out of gtest macro arguments: MSVC processes escape
// sequences inside raw strings passed to macros (C2017 / C3850).

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
    const std::string in1 = R"({"label":"Signal A"})";
    EXPECT_EQ(get_label(in1), "Signal A");
    const std::string in2 = R"({"label":""})";
    EXPECT_EQ(get_label(in2), "");
}

TEST(AutomationJsonGetString, DecodesEmDashEscape)
{
    // What Python's json.dumps() emits by default (ensure_ascii=True).
    const std::string in3 = R"({"label":"Signal A \u2014 auto palette"})";
    EXPECT_EQ(get_label(in3), "Signal A \xE2\x80\x94 auto palette");
}

TEST(AutomationJsonGetString, DecodesGreekAndMicro)
{
    const std::string in4 = R"({"label":"\u0394t"})";
    EXPECT_EQ(get_label(in4), "\xCE\x94t");   // Δt
    const std::string in5 = R"({"label":"\u00b5s"})";
    EXPECT_EQ(get_label(in5), "\xC2\xB5s");   // µs (micro sign)
    const std::string in6 = R"({"label":"\u03BC"})";
    EXPECT_EQ(get_label(in6), "\xCE\xBC");   // μ, upper-case hex
}

TEST(AutomationJsonGetString, DecodesSimpleEscapes)
{
    const std::string in7 = R"({"label":"say \"hi\""})";
    EXPECT_EQ(get_label(in7), "say \"hi\"");
    const std::string in8 = R"({"label":"line1\nline2"})";
    EXPECT_EQ(get_label(in8), "line1\nline2");
    const std::string in9 = R"({"label":"C:\\temp\\x"})";
    EXPECT_EQ(get_label(in9), "C:\\temp\\x");
    const std::string in10 = R"({"label":"a\/b"})";
    EXPECT_EQ(get_label(in10), "a/b");
    const std::string in11 = R"({"label":"\b\f\r\t"})";
    EXPECT_EQ(get_label(in11), "\b\f\r\t");
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
    const std::string in12 = R"({"label":"\uD83D\uDE00"})";
    EXPECT_EQ(get_label(in12), "\xF0\x9F\x98\x80");
    // U+1D4B3 MATHEMATICAL SCRIPT CAPITAL X = \ud835\udcb3
    const std::string in13 = R"({"label":"x=\ud835\udcb3"})";
    EXPECT_EQ(get_label(in13), "x=\xF0\x9D\x92\xB3");
}

TEST(AutomationJsonGetString, EscapedEqualsRawUtf8)
{
    const std::string escaped = get_label(R"({"label":"\u0394 \u03bc \u2014 \ud83d\ude00"})");
    const std::string raw =
        get_label("{\"label\":\"\xCE\x94 \xCE\xBC \xE2\x80\x94 \xF0\x9F\x98\x80\"}");
    EXPECT_EQ(escaped, raw);
    EXPECT_EQ(raw, "\xCE\x94 \xCE\xBC \xE2\x80\x94 \xF0\x9F\x98\x80");
}

TEST(AutomationJsonGetString, LoneSurrogatesBecomeReplacementChar)
{
    const std::string fffd = "\xEF\xBF\xBD";
    const std::string in14 = R"({"label":"a\uD83Db"})";
    EXPECT_EQ(get_label(in14), "a" + fffd + "b");   // lone high
    const std::string in15 = R"({"label":"a\uDE00b"})";
    EXPECT_EQ(get_label(in15), "a" + fffd + "b");   // lone low
    const std::string in16 = R"({"label":"\uDE00\uD83D"})";
    EXPECT_EQ(get_label(in16), fffd + fffd);   // reversed
    // High surrogate followed by a non-surrogate escape: replacement + that char.
    const std::string in17 = R"({"label":"\uD83D\u0041"})";
    EXPECT_EQ(get_label(in17), fffd + "A");
    const std::string in18 = R"({"label":"\uD83D"})";
    EXPECT_EQ(get_label(in18), fffd);   // at end
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
    const std::string in19 = R"({"label":"bad\xescape"})";
    const std::string in20 = R"(bad\xescape)";
    EXPECT_EQ(get_label(in19), in20);
    const std::string in21 = R"({"label":"\u12"})";
    const std::string in22 = R"(\u12)";
    EXPECT_EQ(get_label(in21), in22);
    const std::string in23 = R"({"label":"\u12G4z"})";
    const std::string in24 = R"(\u12G4z)";
    EXPECT_EQ(get_label(in23), in24);
    // Unterminated string: decode what is there, do not crash.
    const std::string in25 = R"({"label":"abc\u00e9)";
    EXPECT_EQ(get_label(in25), "abc\xC3\xA9");
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
    const std::string in26 = R"(a\"b\\c)";
    EXPECT_EQ(json_escape("a\"b\\c"), in26);
    const std::string in27 = R"(\n\r\t\b\f)";
    EXPECT_EQ(json_escape("\n\r\t\b\f"), in27);
    const std::string in28 = R"(\u0001\u001f)";
    EXPECT_EQ(json_escape(std::string("\x01\x1f", 2)), in28);
    // Non-ASCII UTF-8 passes through unescaped (valid JSON).
    EXPECT_EQ(json_escape("\xCE\x94 \xE2\x80\x94 \xF0\x9F\x98\x80"),
              "\xCE\x94 \xE2\x80\x94 \xF0\x9F\x98\x80");
}

TEST(AutomationJsonEscape, RoundTripsThroughGetString)
{
    const std::string cases[] = {
        "Signal A \xE2\x80\x94 auto palette",
        "quote \" backslash \\ slash /",
        "multi\nline\ttabbed\r\b\f",
        std::string("ctl\x01\x02\x1f", 6),
        "\xCE\x94t [\xCE\xBCs] \xF0\x9F\x98\x80",
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
    const std::string in29   = R"({"figure_id":1,"label":"a}b \"}\" {c"})";
    EXPECT_EQ(params, in29);
    EXPECT_EQ(get_label(params), "a}b \"}\" {c");
    EXPECT_EQ(json_get_uint64(params, "figure_id"), 1u);
}
