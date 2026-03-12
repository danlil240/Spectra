// test_param_editor_panel.cpp — Unit tests for ParamEditorPanel (F3)
//
// Tests are pure C++ logic — no ROS2 executor or ImGui context needed.
// Uses GTest::gtest_main (no custom main).

#include <gtest/gtest.h>

#include "ui/param_editor_panel.hpp"

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace ros2 = spectra::adapters::ros2;
using ros2::ParamEditorPanel;
using ros2::ParamDescriptor;
using ros2::ParamEntry;
using ros2::ParamType;
using ros2::ParamValue;
using ros2::PresetEntry;
using ros2::UndoEntry;

// ===========================================================================
// Suite 1 — ParamType helpers
// ===========================================================================

TEST(ParamTypeName, AllTypes)
{
    EXPECT_STREQ(ros2::param_type_name(ParamType::NotSet), "not_set");
    EXPECT_STREQ(ros2::param_type_name(ParamType::Bool), "bool");
    EXPECT_STREQ(ros2::param_type_name(ParamType::Integer), "int64");
    EXPECT_STREQ(ros2::param_type_name(ParamType::Double), "double");
    EXPECT_STREQ(ros2::param_type_name(ParamType::String), "string");
    EXPECT_STREQ(ros2::param_type_name(ParamType::ByteArray), "byte[]");
    EXPECT_STREQ(ros2::param_type_name(ParamType::BoolArray), "bool[]");
    EXPECT_STREQ(ros2::param_type_name(ParamType::IntegerArray), "int64[]");
    EXPECT_STREQ(ros2::param_type_name(ParamType::DoubleArray), "double[]");
    EXPECT_STREQ(ros2::param_type_name(ParamType::StringArray), "string[]");
}

TEST(ParamTypeName, UnknownIntReturnsUnknown)
{
    // Cast an out-of-range value to verify no UB crash
    const char* s = ros2::param_type_name(static_cast<ParamType>(255));
    EXPECT_STREQ(s, "unknown");
}

// ===========================================================================
// Suite 2 — from_rcl_type
// ===========================================================================

TEST(FromRclType, AllMappings)
{
    using PT = rcl_interfaces::msg::ParameterType;
    EXPECT_EQ(ParamEditorPanel::from_rcl_type(PT::PARAMETER_BOOL), ParamType::Bool);
    EXPECT_EQ(ParamEditorPanel::from_rcl_type(PT::PARAMETER_INTEGER), ParamType::Integer);
    EXPECT_EQ(ParamEditorPanel::from_rcl_type(PT::PARAMETER_DOUBLE), ParamType::Double);
    EXPECT_EQ(ParamEditorPanel::from_rcl_type(PT::PARAMETER_STRING), ParamType::String);
    EXPECT_EQ(ParamEditorPanel::from_rcl_type(PT::PARAMETER_BYTE_ARRAY), ParamType::ByteArray);
    EXPECT_EQ(ParamEditorPanel::from_rcl_type(PT::PARAMETER_BOOL_ARRAY), ParamType::BoolArray);
    EXPECT_EQ(ParamEditorPanel::from_rcl_type(PT::PARAMETER_INTEGER_ARRAY),
              ParamType::IntegerArray);
    EXPECT_EQ(ParamEditorPanel::from_rcl_type(PT::PARAMETER_DOUBLE_ARRAY), ParamType::DoubleArray);
    EXPECT_EQ(ParamEditorPanel::from_rcl_type(PT::PARAMETER_STRING_ARRAY), ParamType::StringArray);
    EXPECT_EQ(ParamEditorPanel::from_rcl_type(PT::PARAMETER_NOT_SET), ParamType::NotSet);
    EXPECT_EQ(ParamEditorPanel::from_rcl_type(255), ParamType::NotSet);
}

// ===========================================================================
// Suite 3 — ParamValue construction
// ===========================================================================

TEST(ParamValueConstruct, DefaultIsNotSet)
{
    ParamValue v;
    EXPECT_EQ(v.type, ParamType::NotSet);
    EXPECT_FALSE(v.bool_val);
    EXPECT_EQ(v.int_val, 0);
    EXPECT_DOUBLE_EQ(v.double_val, 0.0);
    EXPECT_TRUE(v.string_val.empty());
}

TEST(ParamValueConstruct, BoolFromMsg)
{
    rcl_interfaces::msg::ParameterValue msg;
    msg.type       = rcl_interfaces::msg::ParameterType::PARAMETER_BOOL;
    msg.bool_value = true;
    auto v         = ParamValue::from_msg(msg);
    EXPECT_EQ(v.type, ParamType::Bool);
    EXPECT_TRUE(v.bool_val);
}

TEST(ParamValueConstruct, IntegerFromMsg)
{
    rcl_interfaces::msg::ParameterValue msg;
    msg.type          = rcl_interfaces::msg::ParameterType::PARAMETER_INTEGER;
    msg.integer_value = 42;
    auto v            = ParamValue::from_msg(msg);
    EXPECT_EQ(v.type, ParamType::Integer);
    EXPECT_EQ(v.int_val, 42);
}

TEST(ParamValueConstruct, DoubleFromMsg)
{
    rcl_interfaces::msg::ParameterValue msg;
    msg.type         = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE;
    msg.double_value = 3.14;
    auto v           = ParamValue::from_msg(msg);
    EXPECT_EQ(v.type, ParamType::Double);
    EXPECT_DOUBLE_EQ(v.double_val, 3.14);
}

TEST(ParamValueConstruct, StringFromMsg)
{
    rcl_interfaces::msg::ParameterValue msg;
    msg.type         = rcl_interfaces::msg::ParameterType::PARAMETER_STRING;
    msg.string_value = "hello_ros";
    auto v           = ParamValue::from_msg(msg);
    EXPECT_EQ(v.type, ParamType::String);
    EXPECT_EQ(v.string_val, "hello_ros");
}

TEST(ParamValueConstruct, IntegerArrayFromMsg)
{
    rcl_interfaces::msg::ParameterValue msg;
    msg.type                = rcl_interfaces::msg::ParameterType::PARAMETER_INTEGER_ARRAY;
    msg.integer_array_value = {1, 2, 3};
    auto v                  = ParamValue::from_msg(msg);
    EXPECT_EQ(v.type, ParamType::IntegerArray);
    ASSERT_EQ(v.int_array.size(), 3u);
    EXPECT_EQ(v.int_array[0], 1);
    EXPECT_EQ(v.int_array[2], 3);
}

TEST(ParamValueConstruct, DoubleArrayFromMsg)
{
    rcl_interfaces::msg::ParameterValue msg;
    msg.type               = rcl_interfaces::msg::ParameterType::PARAMETER_DOUBLE_ARRAY;
    msg.double_array_value = {1.0, 2.0, 3.0};
    auto v                 = ParamValue::from_msg(msg);
    EXPECT_EQ(v.type, ParamType::DoubleArray);
    ASSERT_EQ(v.double_array.size(), 3u);
    EXPECT_DOUBLE_EQ(v.double_array[1], 2.0);
}

TEST(ParamValueConstruct, StringArrayFromMsg)
{
    rcl_interfaces::msg::ParameterValue msg;
    msg.type               = rcl_interfaces::msg::ParameterType::PARAMETER_STRING_ARRAY;
    msg.string_array_value = {"a", "b", "c"};
    auto v                 = ParamValue::from_msg(msg);
    EXPECT_EQ(v.type, ParamType::StringArray);
    ASSERT_EQ(v.string_array.size(), 3u);
    EXPECT_EQ(v.string_array[0], "a");
}

TEST(ParamValueConstruct, BoolArrayFromMsg)
{
    rcl_interfaces::msg::ParameterValue msg;
    msg.type             = rcl_interfaces::msg::ParameterType::PARAMETER_BOOL_ARRAY;
    msg.bool_array_value = {true, false, true};
    auto v               = ParamValue::from_msg(msg);
    EXPECT_EQ(v.type, ParamType::BoolArray);
    ASSERT_EQ(v.bool_array.size(), 3u);
    EXPECT_TRUE(v.bool_array[0]);
    EXPECT_FALSE(v.bool_array[1]);
}

// ===========================================================================
// Suite 4 — ParamValue to_msg round-trip
// ===========================================================================

TEST(ParamValueRoundTrip, Bool)
{
    ParamValue v;
    v.type     = ParamType::Bool;
    v.bool_val = true;
    auto msg   = v.to_msg();
    EXPECT_EQ(msg.type, rcl_interfaces::msg::ParameterType::PARAMETER_BOOL);
    EXPECT_TRUE(msg.bool_value);
    auto v2 = ParamValue::from_msg(msg);
    EXPECT_EQ(v2, v);
}

TEST(ParamValueRoundTrip, Integer)
{
    ParamValue v;
    v.type    = ParamType::Integer;
    v.int_val = -12345;
    auto msg  = v.to_msg();
    EXPECT_EQ(msg.integer_value, -12345);
    EXPECT_EQ(ParamValue::from_msg(msg), v);
}

TEST(ParamValueRoundTrip, Double)
{
    ParamValue v;
    v.type       = ParamType::Double;
    v.double_val = 2.718281828;
    auto msg     = v.to_msg();
    EXPECT_DOUBLE_EQ(msg.double_value, 2.718281828);
    EXPECT_EQ(ParamValue::from_msg(msg), v);
}

TEST(ParamValueRoundTrip, String)
{
    ParamValue v;
    v.type       = ParamType::String;
    v.string_val = "robot_description";
    auto msg     = v.to_msg();
    EXPECT_EQ(msg.string_value, "robot_description");
    EXPECT_EQ(ParamValue::from_msg(msg), v);
}

TEST(ParamValueRoundTrip, IntegerArray)
{
    ParamValue v;
    v.type      = ParamType::IntegerArray;
    v.int_array = {10, 20, 30};
    auto msg    = v.to_msg();
    auto v2     = ParamValue::from_msg(msg);
    EXPECT_EQ(v2, v);
}

TEST(ParamValueRoundTrip, DoubleArray)
{
    ParamValue v;
    v.type         = ParamType::DoubleArray;
    v.double_array = {0.1, 0.2, 0.3};
    EXPECT_EQ(ParamValue::from_msg(v.to_msg()), v);
}

TEST(ParamValueRoundTrip, StringArray)
{
    ParamValue v;
    v.type         = ParamType::StringArray;
    v.string_array = {"x", "y", "z"};
    EXPECT_EQ(ParamValue::from_msg(v.to_msg()), v);
}

// ===========================================================================
// Suite 5 — ParamValue equality
// ===========================================================================

TEST(ParamValueEquality, SameBoolEqual)
{
    ParamValue a, b;
    a.type = b.type = ParamType::Bool;
    a.bool_val = b.bool_val = false;
    EXPECT_EQ(a, b);
}

TEST(ParamValueEquality, DifferentBoolNotEqual)
{
    ParamValue a, b;
    a.type = b.type = ParamType::Bool;
    a.bool_val      = true;
    b.bool_val      = false;
    EXPECT_NE(a, b);
}

TEST(ParamValueEquality, DifferentTypesNotEqual)
{
    ParamValue a, b;
    a.type = ParamType::Bool;
    b.type = ParamType::Integer;
    EXPECT_NE(a, b);
}

TEST(ParamValueEquality, SameIntEqual)
{
    ParamValue a, b;
    a.type = b.type = ParamType::Integer;
    a.int_val = b.int_val = 99;
    EXPECT_EQ(a, b);
}

TEST(ParamValueEquality, SameStringEqual)
{
    ParamValue a, b;
    a.type = b.type = ParamType::String;
    a.string_val = b.string_val = "hello";
    EXPECT_EQ(a, b);
}

TEST(ParamValueEquality, IntArrayEqual)
{
    ParamValue a, b;
    a.type = b.type = ParamType::IntegerArray;
    a.int_array = b.int_array = {1, 2, 3};
    EXPECT_EQ(a, b);
    b.int_array = {1, 2, 4};
    EXPECT_NE(a, b);
}

// ===========================================================================
// Suite 6 — ParamValue to_display_string
// ===========================================================================

TEST(ParamValueDisplay, NotSet)
{
    ParamValue v;
    EXPECT_EQ(v.to_display_string(), "<not_set>");
}

TEST(ParamValueDisplay, Bool)
{
    ParamValue v;
    v.type     = ParamType::Bool;
    v.bool_val = true;
    EXPECT_EQ(v.to_display_string(), "true");
    v.bool_val = false;
    EXPECT_EQ(v.to_display_string(), "false");
}

TEST(ParamValueDisplay, Integer)
{
    ParamValue v;
    v.type    = ParamType::Integer;
    v.int_val = -42;
    EXPECT_EQ(v.to_display_string(), "-42");
}

TEST(ParamValueDisplay, Double)
{
    ParamValue v;
    v.type        = ParamType::Double;
    v.double_val  = 3.14;
    std::string s = v.to_display_string();
    EXPECT_FALSE(s.empty());
    EXPECT_NE(s.find("3.14"), std::string::npos);
}

TEST(ParamValueDisplay, String)
{
    ParamValue v;
    v.type       = ParamType::String;
    v.string_val = "hello";
    EXPECT_EQ(v.to_display_string(), "\"hello\"");
}

TEST(ParamValueDisplay, IntegerArrayTruncated)
{
    ParamValue v;
    v.type = ParamType::IntegerArray;
    for (int i = 0; i < 50; ++i)
        v.int_array.push_back(i);
    std::string s = v.to_display_string(16);
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(s[0], '[');
}

TEST(ParamValueDisplay, EmptyStringArray)
{
    ParamValue v;
    v.type = ParamType::StringArray;
    EXPECT_EQ(v.to_display_string(), "[]");
}

// ===========================================================================
// Suite 7 — ParamDescriptor
// ===========================================================================

TEST(ParamDescriptor, DefaultNoRange)
{
    ParamDescriptor d;
    EXPECT_FALSE(d.read_only);
    EXPECT_FALSE(d.has_float_range());
    EXPECT_FALSE(d.has_integer_range());
}

TEST(ParamDescriptor, FromMsgReadOnly)
{
    rcl_interfaces::msg::ParameterDescriptor msg;
    msg.read_only   = true;
    msg.description = "test param";
    auto d          = ParamDescriptor::from_msg(msg);
    EXPECT_TRUE(d.read_only);
    EXPECT_EQ(d.description, "test param");
}

TEST(ParamDescriptor, FromMsgFloatRange)
{
    rcl_interfaces::msg::ParameterDescriptor msg;
    rcl_interfaces::msg::FloatingPointRange  fr;
    fr.from_value = -1.0;
    fr.to_value   = 1.0;
    fr.step       = 0.01;
    msg.floating_point_range.push_back(fr);
    auto d = ParamDescriptor::from_msg(msg);
    EXPECT_TRUE(d.has_float_range());
    EXPECT_DOUBLE_EQ(d.float_range_min, -1.0);
    EXPECT_DOUBLE_EQ(d.float_range_max, 1.0);
    EXPECT_DOUBLE_EQ(d.float_range_step, 0.01);
}

TEST(ParamDescriptor, FromMsgIntegerRange)
{
    rcl_interfaces::msg::ParameterDescriptor msg;
    rcl_interfaces::msg::IntegerRange        ir;
    ir.from_value = 0;
    ir.to_value   = 100;
    ir.step       = 1;
    msg.integer_range.push_back(ir);
    auto d = ParamDescriptor::from_msg(msg);
    EXPECT_TRUE(d.has_integer_range());
    EXPECT_EQ(d.integer_range_min, 0);
    EXPECT_EQ(d.integer_range_max, 100);
}

TEST(ParamDescriptor, HasFloatRangeOnlyWhenNonZero)
{
    ParamDescriptor d;
    d.float_range_min = 0.0;
    d.float_range_max = 0.0;
    EXPECT_FALSE(d.has_float_range());
    d.float_range_max = 1.0;
    EXPECT_TRUE(d.has_float_range());
}

// ===========================================================================
// Suite 8 — YAML serialization
// ===========================================================================

TEST(YamlSerialize, EmptyParams)
{
    std::unordered_map<std::string, ParamValue> params;
    std::string yaml = ParamEditorPanel::serialize_yaml("/my_node", params);
    // Should still produce the node key header
    EXPECT_NE(yaml.find("my_node"), std::string::npos);
    EXPECT_NE(yaml.find("ros__parameters"), std::string::npos);
}

TEST(YamlSerialize, BoolParam)
{
    std::unordered_map<std::string, ParamValue> params;
    ParamValue                                  v;
    v.type                 = ParamType::Bool;
    v.bool_val             = true;
    params["use_sim_time"] = v;
    std::string yaml       = ParamEditorPanel::serialize_yaml("/my_node", params);
    EXPECT_NE(yaml.find("use_sim_time: true"), std::string::npos);
}

TEST(YamlSerialize, IntegerParam)
{
    std::unordered_map<std::string, ParamValue> params;
    ParamValue                                  v;
    v.type             = ParamType::Integer;
    v.int_val          = 42;
    params["max_iter"] = v;
    std::string yaml   = ParamEditorPanel::serialize_yaml("/my_node", params);
    EXPECT_NE(yaml.find("max_iter: 42"), std::string::npos);
}

TEST(YamlSerialize, DoubleParam)
{
    std::unordered_map<std::string, ParamValue> params;
    ParamValue                                  v;
    v.type           = ParamType::Double;
    v.double_val     = 0.5;
    params["gain"]   = v;
    std::string yaml = ParamEditorPanel::serialize_yaml("/my_node", params);
    EXPECT_NE(yaml.find("gain:"), std::string::npos);
    EXPECT_NE(yaml.find("0.5"), std::string::npos);
}

TEST(YamlSerialize, StringParam)
{
    std::unordered_map<std::string, ParamValue> params;
    ParamValue                                  v;
    v.type             = ParamType::String;
    v.string_val       = "odom";
    params["frame_id"] = v;
    std::string yaml   = ParamEditorPanel::serialize_yaml("/my_node", params);
    EXPECT_NE(yaml.find("frame_id: odom"), std::string::npos);
}

TEST(YamlSerialize, IntegerArrayParam)
{
    std::unordered_map<std::string, ParamValue> params;
    ParamValue                                  v;
    v.type            = ParamType::IntegerArray;
    v.int_array       = {1, 2, 3};
    params["indices"] = v;
    std::string yaml  = ParamEditorPanel::serialize_yaml("/my_node", params);
    EXPECT_NE(yaml.find("indices:"), std::string::npos);
    EXPECT_NE(yaml.find("["), std::string::npos);
}

TEST(YamlSerialize, StringArrayParam)
{
    std::unordered_map<std::string, ParamValue> params;
    ParamValue                                  v;
    v.type           = ParamType::StringArray;
    v.string_array   = {"topic_a", "topic_b"};
    params["topics"] = v;
    std::string yaml = ParamEditorPanel::serialize_yaml("/my_node", params);
    EXPECT_NE(yaml.find("topics:"), std::string::npos);
    EXPECT_NE(yaml.find("topic_a"), std::string::npos);
}

TEST(YamlSerialize, NodeNameStripsLeadingSlash)
{
    std::unordered_map<std::string, ParamValue> params;
    ParamValue                                  v;
    v.type            = ParamType::Bool;
    v.bool_val        = false;
    params["enabled"] = v;
    std::string yaml  = ParamEditorPanel::serialize_yaml("/robot/controller", params);
    // Leading slash stripped: "robot/controller:" should appear at line start
    EXPECT_NE(yaml.find("robot/controller:"), std::string::npos);
}

// ===========================================================================
// Suite 9 — YAML parsing
// ===========================================================================

static const std::string kSimpleYaml = R"(
my_node:
  ros__parameters:
    use_sim_time: false
    max_iterations: 100
    gain: 0.75
    frame_id: base_link
)";

TEST(YamlParse, BoolParam)
{
    std::unordered_map<std::string, ParamValue> out;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(kSimpleYaml, "my_node", out, err)) << err;
    ASSERT_TRUE(out.count("use_sim_time"));
    EXPECT_EQ(out["use_sim_time"].type, ParamType::Bool);
    EXPECT_FALSE(out["use_sim_time"].bool_val);
}

TEST(YamlParse, IntegerParam)
{
    std::unordered_map<std::string, ParamValue> out;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(kSimpleYaml, "my_node", out, err)) << err;
    ASSERT_TRUE(out.count("max_iterations"));
    EXPECT_EQ(out["max_iterations"].type, ParamType::Integer);
    EXPECT_EQ(out["max_iterations"].int_val, 100);
}

TEST(YamlParse, DoubleParam)
{
    std::unordered_map<std::string, ParamValue> out;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(kSimpleYaml, "my_node", out, err)) << err;
    ASSERT_TRUE(out.count("gain"));
    EXPECT_EQ(out["gain"].type, ParamType::Double);
    EXPECT_NEAR(out["gain"].double_val, 0.75, 1e-9);
}

TEST(YamlParse, StringParam)
{
    std::unordered_map<std::string, ParamValue> out;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(kSimpleYaml, "my_node", out, err)) << err;
    ASSERT_TRUE(out.count("frame_id"));
    EXPECT_EQ(out["frame_id"].type, ParamType::String);
    EXPECT_EQ(out["frame_id"].string_val, "base_link");
}

TEST(YamlParse, IntegerArrayFlowNotation)
{
    const std::string                           yaml = R"(
node:
  ros__parameters:
    dims: [10, 20, 30]
)";
    std::unordered_map<std::string, ParamValue> out;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(yaml, "node", out, err)) << err;
    ASSERT_TRUE(out.count("dims"));
    EXPECT_EQ(out["dims"].type, ParamType::IntegerArray);
    ASSERT_EQ(out["dims"].int_array.size(), 3u);
    EXPECT_EQ(out["dims"].int_array[0], 10);
    EXPECT_EQ(out["dims"].int_array[2], 30);
}

TEST(YamlParse, DoubleArrayFlowNotation)
{
    const std::string                           yaml = R"(
node:
  ros__parameters:
    weights: [0.1, 0.2, 0.7]
)";
    std::unordered_map<std::string, ParamValue> out;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(yaml, "node", out, err)) << err;
    ASSERT_TRUE(out.count("weights"));
    EXPECT_EQ(out["weights"].type, ParamType::DoubleArray);
    ASSERT_EQ(out["weights"].double_array.size(), 3u);
    EXPECT_NEAR(out["weights"].double_array[0], 0.1, 1e-9);
}

TEST(YamlParse, BoolArrayFlowNotation)
{
    const std::string                           yaml = R"(
node:
  ros__parameters:
    flags: [true, false, true]
)";
    std::unordered_map<std::string, ParamValue> out;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(yaml, "node", out, err)) << err;
    ASSERT_TRUE(out.count("flags"));
    EXPECT_EQ(out["flags"].type, ParamType::BoolArray);
    ASSERT_EQ(out["flags"].bool_array.size(), 3u);
    EXPECT_TRUE(out["flags"].bool_array[0]);
    EXPECT_FALSE(out["flags"].bool_array[1]);
}

TEST(YamlParse, StringArrayFlowNotation)
{
    const std::string                           yaml = R"(
node:
  ros__parameters:
    topics: ["scan", "odom"]
)";
    std::unordered_map<std::string, ParamValue> out;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(yaml, "node", out, err)) << err;
    ASSERT_TRUE(out.count("topics"));
    EXPECT_EQ(out["topics"].type, ParamType::StringArray);
    ASSERT_EQ(out["topics"].string_array.size(), 2u);
    EXPECT_EQ(out["topics"].string_array[0], "scan");
}

TEST(YamlParse, QuotedStringParam)
{
    const std::string                           yaml = R"(
node:
  ros__parameters:
    name: "my robot"
)";
    std::unordered_map<std::string, ParamValue> out;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(yaml, "node", out, err)) << err;
    ASSERT_TRUE(out.count("name"));
    EXPECT_EQ(out["name"].type, ParamType::String);
    EXPECT_EQ(out["name"].string_val, "my robot");
}

TEST(YamlParse, TrueVariants)
{
    const std::string                           yaml = R"(
node:
  ros__parameters:
    a: true
    b: True
)";
    std::unordered_map<std::string, ParamValue> out;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(yaml, "node", out, err)) << err;
    EXPECT_TRUE(out["a"].bool_val);
    EXPECT_TRUE(out["b"].bool_val);
}

TEST(YamlParse, CommentsStripped)
{
    const std::string                           yaml = R"(
node:
  ros__parameters:
    gain: 1.0  # important gain value
)";
    std::unordered_map<std::string, ParamValue> out;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(yaml, "node", out, err)) << err;
    ASSERT_TRUE(out.count("gain"));
    EXPECT_NEAR(out["gain"].double_val, 1.0, 1e-9);
}

TEST(YamlParse, EmptyYamlReturnsFalse)
{
    std::unordered_map<std::string, ParamValue> out;
    std::string                                 err;
    bool result = ParamEditorPanel::parse_yaml("", "node", out, err);
    EXPECT_FALSE(result);
    EXPECT_FALSE(err.empty());
}

// ===========================================================================
// Suite 10 — YAML round-trip
// ===========================================================================

TEST(YamlRoundTrip, BoolRoundTrip)
{
    std::unordered_map<std::string, ParamValue> orig;
    ParamValue                                  v;
    v.type       = ParamType::Bool;
    v.bool_val   = true;
    orig["flag"] = v;

    std::string                                 yaml = ParamEditorPanel::serialize_yaml("/n", orig);
    std::unordered_map<std::string, ParamValue> parsed;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(yaml, "/n", parsed, err)) << err;
    ASSERT_TRUE(parsed.count("flag"));
    EXPECT_EQ(parsed["flag"].bool_val, true);
}

TEST(YamlRoundTrip, IntegerRoundTrip)
{
    std::unordered_map<std::string, ParamValue> orig;
    ParamValue                                  v;
    v.type        = ParamType::Integer;
    v.int_val     = -9999;
    orig["count"] = v;

    std::string                                 yaml = ParamEditorPanel::serialize_yaml("/n", orig);
    std::unordered_map<std::string, ParamValue> parsed;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(yaml, "/n", parsed, err)) << err;
    EXPECT_EQ(parsed["count"].int_val, -9999);
}

TEST(YamlRoundTrip, DoubleRoundTrip)
{
    std::unordered_map<std::string, ParamValue> orig;
    ParamValue                                  v;
    v.type       = ParamType::Double;
    v.double_val = 1.23456789;
    orig["tol"]  = v;

    std::string                                 yaml = ParamEditorPanel::serialize_yaml("/n", orig);
    std::unordered_map<std::string, ParamValue> parsed;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(yaml, "/n", parsed, err)) << err;
    EXPECT_NEAR(parsed["tol"].double_val, 1.23456789, 1e-6);
}

TEST(YamlRoundTrip, IntegerArrayRoundTrip)
{
    std::unordered_map<std::string, ParamValue> orig;
    ParamValue                                  v;
    v.type        = ParamType::IntegerArray;
    v.int_array   = {5, 10, 15};
    orig["sizes"] = v;

    std::string                                 yaml = ParamEditorPanel::serialize_yaml("/n", orig);
    std::unordered_map<std::string, ParamValue> parsed;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(yaml, "/n", parsed, err)) << err;
    ASSERT_EQ(parsed["sizes"].int_array.size(), 3u);
    EXPECT_EQ(parsed["sizes"].int_array[1], 10);
}

// ===========================================================================
// Suite 11 — PresetEntry
// ===========================================================================

TEST(PresetEntry, ValidWhenNameAndPathSet)
{
    ros2::PresetEntry e;
    e.name      = "default";
    e.yaml_path = "/tmp/test.yaml";
    EXPECT_TRUE(e.valid());
}

TEST(PresetEntry, InvalidWhenEmpty)
{
    ros2::PresetEntry e;
    EXPECT_FALSE(e.valid());
}

TEST(PresetEntry, InvalidWhenOnlyName)
{
    ros2::PresetEntry e;
    e.name = "test";
    EXPECT_FALSE(e.valid());
}

// ===========================================================================
// Suite 12 — UndoEntry
// ===========================================================================

TEST(UndoEntry, DefaultInvalid)
{
    UndoEntry u;
    EXPECT_FALSE(u.valid);
}

TEST(UndoEntry, FieldsStored)
{
    UndoEntry u;
    u.valid      = true;
    u.node_name  = "/my_node";
    u.param_name = "gain";
    ParamValue before;
    before.type       = ParamType::Double;
    before.double_val = 0.5;
    ParamValue after;
    after.type       = ParamType::Double;
    after.double_val = 1.0;
    u.before         = before;
    u.after          = after;
    EXPECT_DOUBLE_EQ(u.before.double_val, 0.5);
    EXPECT_DOUBLE_EQ(u.after.double_val, 1.0);
    EXPECT_EQ(u.param_name, "gain");
}

// ===========================================================================
// Suite 13 — ParamEntry
// ===========================================================================

TEST(ParamEntry, DefaultConstruction)
{
    ParamEntry e;
    EXPECT_TRUE(e.name.empty());
    EXPECT_EQ(e.type, ParamType::NotSet);
    EXPECT_FALSE(e.staged_dirty);
    EXPECT_FALSE(e.set_error);
}

TEST(ParamEntry, StagedDirtyDetection)
{
    ParamEntry e;
    e.name               = "gain";
    e.type               = ParamType::Double;
    e.current.type       = ParamType::Double;
    e.current.double_val = 1.0;
    e.staged             = e.current;
    EXPECT_FALSE(e.staged_dirty);

    e.staged.double_val = 2.0;
    e.staged_dirty      = (e.staged != e.current);
    EXPECT_TRUE(e.staged_dirty);
}

// ===========================================================================
// Suite 14 — Construction without a live node (no rclcpp runtime)
// ===========================================================================
// We pass nullptr as the node to test all non-ROS paths safely.

TEST(ParamEditorPanelNoRos, ConstructionWithNullNode)
{
    ParamEditorPanel panel(nullptr);
    EXPECT_EQ(panel.target_node(), "");
    EXPECT_EQ(panel.title(), "Parameter Editor");
    EXPECT_TRUE(panel.live_edit());
    EXPECT_FALSE(panel.is_loaded());
    EXPECT_FALSE(panel.is_refreshing());
    EXPECT_TRUE(panel.last_error().empty());
    EXPECT_EQ(panel.staged_count(), 0u);
    EXPECT_FALSE(panel.can_undo());
}

TEST(ParamEditorPanelNoRos, SetTargetNodeStoresName)
{
    ParamEditorPanel panel(nullptr);
    panel.set_target_node("/robot/controller");
    EXPECT_EQ(panel.target_node(), "/robot/controller");
}

TEST(ParamEditorPanelNoRos, SetTargetNodeClearsState)
{
    ParamEditorPanel panel(nullptr);
    panel.set_target_node("/node_a");
    panel.set_target_node("/node_b");
    EXPECT_EQ(panel.target_node(), "/node_b");
    EXPECT_FALSE(panel.is_loaded());
}

TEST(ParamEditorPanelNoRos, SetLiveEditToggle)
{
    ParamEditorPanel panel(nullptr);
    panel.set_live_edit(false);
    EXPECT_FALSE(panel.live_edit());
    panel.set_live_edit(true);
    EXPECT_TRUE(panel.live_edit());
}

TEST(ParamEditorPanelNoRos, SetTitle)
{
    ParamEditorPanel panel(nullptr);
    panel.set_title("My Params");
    EXPECT_EQ(panel.title(), "My Params");
}

TEST(ParamEditorPanelNoRos, ParamNamesEmptyBeforeLoad)
{
    ParamEditorPanel panel(nullptr);
    EXPECT_TRUE(panel.param_names().empty());
}

TEST(ParamEditorPanelNoRos, ParamEntryNotFoundReturnsEmpty)
{
    ParamEditorPanel panel(nullptr);
    auto             e = panel.param_entry("nonexistent");
    EXPECT_TRUE(e.name.empty());
}

TEST(ParamEditorPanelNoRos, CannotUndoInitially)
{
    ParamEditorPanel panel(nullptr);
    EXPECT_FALSE(panel.can_undo());
    EXPECT_FALSE(panel.undo_last());
}

TEST(ParamEditorPanelNoRos, ApplyStagedWithNoParamsReturnsTrue)
{
    ParamEditorPanel panel(nullptr);
    EXPECT_TRUE(panel.apply_staged());
}

TEST(ParamEditorPanelNoRos, DiscardStagedNoOp)
{
    ParamEditorPanel panel(nullptr);
    // Should not crash with empty state
    panel.discard_staged();
    EXPECT_EQ(panel.staged_count(), 0u);
}

TEST(ParamEditorPanelNoRos, ClearError)
{
    ParamEditorPanel panel(nullptr);
    panel.clear_error();
    EXPECT_TRUE(panel.last_error().empty());
}

TEST(ParamEditorPanelNoRos, DrawNoImguiContextNoCrash)
{
    // Non-ImGui build: draw() compiles to a no-op
    // ImGui build: draw() requires an active context, but we test the
    //              non-crash contract here (nullptr p_open)
    ParamEditorPanel panel(nullptr);
#ifndef SPECTRA_USE_IMGUI
    panel.draw(nullptr);   // must not crash
#endif
}

// ===========================================================================
// Suite 15 — Preset management (in-memory, no ROS runtime)
// ===========================================================================

TEST(PresetManagement, AddAndRetrieve)
{
    ParamEditorPanel  panel(nullptr);
    ros2::PresetEntry e;
    e.name      = "defaults";
    e.node_name = "/robot";
    e.yaml_path = "/tmp/defaults.yaml";
    panel.add_preset(e);
    ASSERT_EQ(panel.presets().size(), 1u);
    EXPECT_EQ(panel.presets()[0].name, "defaults");
}

TEST(PresetManagement, RemoveByIndex)
{
    ParamEditorPanel  panel(nullptr);
    ros2::PresetEntry e1, e2;
    e1.name      = "a";
    e1.yaml_path = "/a.yaml";
    e2.name      = "b";
    e2.yaml_path = "/b.yaml";
    panel.add_preset(e1);
    panel.add_preset(e2);
    EXPECT_EQ(panel.presets().size(), 2u);
    panel.remove_preset(0);
    ASSERT_EQ(panel.presets().size(), 1u);
    EXPECT_EQ(panel.presets()[0].name, "b");
}

TEST(PresetManagement, RemoveOutOfBoundsNoOp)
{
    ParamEditorPanel panel(nullptr);
    panel.remove_preset(99);   // no crash, no effect
    EXPECT_EQ(panel.presets().size(), 0u);
}

TEST(PresetManagement, SavePresetFailsWithNoNode)
{
    ParamEditorPanel panel(nullptr);
    // No target node → nothing to save
    bool ok = panel.save_preset("test", "/tmp/test_param_save.yaml");
    EXPECT_FALSE(ok);
}

// ===========================================================================
// Suite 16 — Callbacks (no ROS runtime)
// ===========================================================================

TEST(Callbacks, RefreshDoneCallbackStored)
{
    ParamEditorPanel panel(nullptr);
    bool             called = false;
    panel.set_on_refresh_done([&](bool) { called = true; });
    // Callback not fired without a real refresh — just verify it was stored
    EXPECT_FALSE(called);
}

TEST(Callbacks, ParamSetCallbackStored)
{
    ParamEditorPanel panel(nullptr);
    bool             called = false;
    panel.set_on_param_set([&](const std::string&, const ParamValue&, bool) { called = true; });
    EXPECT_FALSE(called);
}

// ===========================================================================
// Suite 17 — Edge cases
// ===========================================================================

TEST(EdgeCases, RefreshWithNoNodeNoCrash)
{
    ParamEditorPanel panel(nullptr);
    panel.set_target_node("");
    // Should return quickly without crashing (no set_client)
    panel.refresh();
    EXPECT_FALSE(panel.is_loaded());
}

TEST(EdgeCases, SetTargetNodeEmptyStringClearsState)
{
    ParamEditorPanel panel(nullptr);
    panel.set_target_node("/some_node");
    panel.set_target_node("");
    EXPECT_EQ(panel.target_node(), "");
    EXPECT_FALSE(panel.is_loaded());
}

TEST(EdgeCases, MultipleSetTargetNodeCalls)
{
    ParamEditorPanel panel(nullptr);
    for (int i = 0; i < 5; ++i)
    {
        panel.set_target_node("/node_" + std::to_string(i));
    }
    EXPECT_EQ(panel.target_node(), "/node_4");
}

TEST(EdgeCases, ParseYamlNegativeInteger)
{
    const std::string                           yaml = R"(
node:
  ros__parameters:
    offset: -255
)";
    std::unordered_map<std::string, ParamValue> out;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(yaml, "node", out, err)) << err;
    EXPECT_EQ(out["offset"].int_val, -255);
}

TEST(EdgeCases, ParseYamlNegativeDouble)
{
    const std::string                           yaml = R"(
node:
  ros__parameters:
    bias: -0.001
)";
    std::unordered_map<std::string, ParamValue> out;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(yaml, "node", out, err)) << err;
    EXPECT_NEAR(out["bias"].double_val, -0.001, 1e-9);
}

TEST(EdgeCases, ParseYamlZeroValues)
{
    const std::string                           yaml = R"(
node:
  ros__parameters:
    count: 0
    rate: 0.0
)";
    std::unordered_map<std::string, ParamValue> out;
    std::string                                 err;
    ASSERT_TRUE(ParamEditorPanel::parse_yaml(yaml, "node", out, err)) << err;
    EXPECT_EQ(out["count"].type, ParamType::Integer);
    EXPECT_EQ(out["count"].int_val, 0);
    EXPECT_EQ(out["rate"].type, ParamType::Double);
    EXPECT_DOUBLE_EQ(out["rate"].double_val, 0.0);
}
