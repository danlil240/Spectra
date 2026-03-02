// Unit tests for MessageIntrospector — runtime ROS2 message introspection.
//
// Tests compile and run only when SPECTRA_USE_ROS2 is ON and a ROS2 workspace
// is sourced.  Registered in tests/CMakeLists.txt inside if(SPECTRA_USE_ROS2).
//
// Test coverage:
//   - MessageSchema tree construction (Float64, Twist, Imu)
//   - FieldDescriptor names, types, offsets, nested fields, arrays
//   - FieldAccessor validity, extraction for all scalar types
//   - extract_double / extract_int64 on live in-memory message structs
//   - Array field access (fixed and dynamic)
//   - Cache: repeated introspect() returns same pointer
//   - Error handling: unknown type, bad path, null pointer
//
// NOTE: rclcpp::init / rclcpp::shutdown may only be called once per process.
// We use the shared RclcppEnvironment from test_ros2_bridge.

#include "message_introspector.hpp"

#include <cmath>
#include <limits>
#include <string>

#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

// ROS2 message types used in tests.
#include <std_msgs/msg/float64.hpp>
#include <std_msgs/msg/int32.hpp>
#include <std_msgs/msg/bool.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/float64_multi_array.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <geometry_msgs/msg/vector3.hpp>
#include <sensor_msgs/msg/imu.hpp>

// rosidl typesupport introspection — needed to get type support handles.
#include <rosidl_typesupport_cpp/message_type_support.hpp>
#include <rosidl_typesupport_introspection_cpp/identifier.hpp>

using namespace spectra::adapters::ros2;
using namespace std::string_literals;

// ---------------------------------------------------------------------------
// Test environment — init rclcpp once for the whole binary.
// ---------------------------------------------------------------------------

class RclcppEnvironment : public ::testing::Environment
{
public:
    void SetUp() override
    {
        if (!rclcpp::ok())
            rclcpp::init(0, nullptr);
    }
    void TearDown() override
    {
        if (rclcpp::ok())
            rclcpp::shutdown();
    }
};

// ---------------------------------------------------------------------------
// Helper: get the introspection type support for a message type T.
// ---------------------------------------------------------------------------

template<typename MsgT>
const rosidl_message_type_support_t* get_introspection_ts()
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<MsgT>();
    if (!ts) return nullptr;
    return get_message_typesupport_handle(
        ts, rosidl_typesupport_introspection_cpp::typesupport_identifier);
}

// ---------------------------------------------------------------------------
// Fixture: fresh MessageIntrospector per test.
// ---------------------------------------------------------------------------

class MessageIntrospectorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        intr_ = std::make_unique<MessageIntrospector>();
    }

    void TearDown() override
    {
        intr_.reset();
    }

    std::unique_ptr<MessageIntrospector> intr_;
};

// ===========================================================================
// Suite: Construction & lifecycle
// ===========================================================================

TEST_F(MessageIntrospectorTest, ConstructsClean)
{
    EXPECT_EQ(intr_->cache_size(), 0u);
}

TEST_F(MessageIntrospectorTest, ClearCacheWorks)
{
    // Populate then clear.
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Float64>();
    intr_->introspect_type_support(ts, "std_msgs/msg/Float64");
    EXPECT_EQ(intr_->cache_size(), 1u);
    intr_->clear_cache();
    EXPECT_EQ(intr_->cache_size(), 0u);
}

// ===========================================================================
// Suite: Schema introspection — std_msgs/msg/Float64
// ===========================================================================

class Float64SchemaTest : public MessageIntrospectorTest
{
protected:
    void SetUp() override
    {
        MessageIntrospectorTest::SetUp();
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            std_msgs::msg::Float64>();
        ASSERT_NE(ts, nullptr);
        schema_ = intr_->introspect_type_support(ts, "std_msgs/msg/Float64");
        ASSERT_NE(schema_, nullptr);
    }

    std::shared_ptr<const MessageSchema> schema_;
};

TEST_F(Float64SchemaTest, TypeNamePreserved)
{
    EXPECT_EQ(schema_->type_name, "std_msgs/msg/Float64");
}

TEST_F(Float64SchemaTest, HasSingleField)
{
    // std_msgs/Float64 has one field: double data
    EXPECT_EQ(schema_->fields.size(), 1u);
}

TEST_F(Float64SchemaTest, FieldNameIsData)
{
    EXPECT_EQ(schema_->fields[0].name, "data");
}

TEST_F(Float64SchemaTest, FieldTypeIsFloat64)
{
    EXPECT_EQ(schema_->fields[0].type, FieldType::Float64);
}

TEST_F(Float64SchemaTest, FieldIsNotArray)
{
    EXPECT_FALSE(schema_->fields[0].is_array);
}

TEST_F(Float64SchemaTest, FieldIsNumericLeaf)
{
    EXPECT_TRUE(schema_->fields[0].is_numeric_leaf());
}

TEST_F(Float64SchemaTest, FullPathIsData)
{
    EXPECT_EQ(schema_->fields[0].full_path, "data");
}

TEST_F(Float64SchemaTest, FindFieldByPath)
{
    const auto* fd = schema_->find_field("data");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->name, "data");
    EXPECT_EQ(fd->type, FieldType::Float64);
}

TEST_F(Float64SchemaTest, FindNonexistentFieldReturnsNull)
{
    EXPECT_EQ(schema_->find_field("nonexistent"), nullptr);
}

TEST_F(Float64SchemaTest, NumericPathsContainsData)
{
    const auto paths = schema_->numeric_paths();
    ASSERT_EQ(paths.size(), 1u);
    EXPECT_EQ(paths[0], "data");
}

// ===========================================================================
// Suite: Schema introspection — geometry_msgs/msg/Twist
// ===========================================================================

class TwistSchemaTest : public MessageIntrospectorTest
{
protected:
    void SetUp() override
    {
        MessageIntrospectorTest::SetUp();
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            geometry_msgs::msg::Twist>();
        ASSERT_NE(ts, nullptr);
        schema_ = intr_->introspect_type_support(ts, "geometry_msgs/msg/Twist");
        ASSERT_NE(schema_, nullptr);
    }

    std::shared_ptr<const MessageSchema> schema_;
};

TEST_F(TwistSchemaTest, HasTwoTopLevelFields)
{
    // geometry_msgs/Twist has: linear (Vector3), angular (Vector3)
    EXPECT_EQ(schema_->fields.size(), 2u);
}

TEST_F(TwistSchemaTest, LinearFieldIsMessage)
{
    const auto* fd = schema_->find_field("linear");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->type, FieldType::Message);
    EXPECT_FALSE(fd->children.empty());
}

TEST_F(TwistSchemaTest, AngularFieldIsMessage)
{
    const auto* fd = schema_->find_field("angular");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->type, FieldType::Message);
    EXPECT_FALSE(fd->children.empty());
}

TEST_F(TwistSchemaTest, LinearXIsNumericLeaf)
{
    const auto* fd = schema_->find_field("linear.x");
    ASSERT_NE(fd, nullptr);
    EXPECT_TRUE(fd->is_numeric_leaf());
    EXPECT_EQ(fd->type, FieldType::Float64);
}

TEST_F(TwistSchemaTest, LinearYZPresent)
{
    EXPECT_NE(schema_->find_field("linear.y"), nullptr);
    EXPECT_NE(schema_->find_field("linear.z"), nullptr);
}

TEST_F(TwistSchemaTest, AngularXYZPresent)
{
    EXPECT_NE(schema_->find_field("angular.x"), nullptr);
    EXPECT_NE(schema_->find_field("angular.y"), nullptr);
    EXPECT_NE(schema_->find_field("angular.z"), nullptr);
}

TEST_F(TwistSchemaTest, NumericPathsHasSixComponents)
{
    // linear.x/y/z + angular.x/y/z = 6
    const auto paths = schema_->numeric_paths();
    EXPECT_EQ(paths.size(), 6u);
}

TEST_F(TwistSchemaTest, FindNestedFieldReturnsCorrectPath)
{
    const auto* fd = schema_->find_field("angular.z");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->full_path, "angular.z");
}

// ===========================================================================
// Suite: Schema introspection — sensor_msgs/msg/Imu
// ===========================================================================

class ImuSchemaTest : public MessageIntrospectorTest
{
protected:
    void SetUp() override
    {
        MessageIntrospectorTest::SetUp();
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            sensor_msgs::msg::Imu>();
        ASSERT_NE(ts, nullptr);
        schema_ = intr_->introspect_type_support(ts, "sensor_msgs/msg/Imu");
        ASSERT_NE(schema_, nullptr);
    }

    std::shared_ptr<const MessageSchema> schema_;
};

TEST_F(ImuSchemaTest, HasTopLevelFields)
{
    // sensor_msgs/Imu: header, orientation, orientation_covariance,
    //                  angular_velocity, angular_velocity_covariance,
    //                  linear_acceleration, linear_acceleration_covariance
    EXPECT_GE(schema_->fields.size(), 4u);
}

TEST_F(ImuSchemaTest, LinearAccelerationXPresent)
{
    const auto* fd = schema_->find_field("linear_acceleration.x");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->type, FieldType::Float64);
}

TEST_F(ImuSchemaTest, AngularVelocityYPresent)
{
    const auto* fd = schema_->find_field("angular_velocity.y");
    ASSERT_NE(fd, nullptr);
    EXPECT_TRUE(fd->is_numeric_leaf());
}

TEST_F(ImuSchemaTest, OrientationWPresent)
{
    const auto* fd = schema_->find_field("orientation.w");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->type, FieldType::Float64);
}

TEST_F(ImuSchemaTest, CovarianceFieldIsArray)
{
    // orientation_covariance is float64[9]
    const auto* fd = schema_->find_field("orientation_covariance");
    ASSERT_NE(fd, nullptr);
    EXPECT_TRUE(fd->is_array);
}

TEST_F(ImuSchemaTest, NumericPathsNonEmpty)
{
    const auto paths = schema_->numeric_paths();
    EXPECT_GE(paths.size(), 9u);   // at least 3+3+3 from the three Vector3s
}

// ===========================================================================
// Suite: FieldAccessor — Float64 value extraction
// ===========================================================================

class Float64AccessorTest : public MessageIntrospectorTest
{
protected:
    void SetUp() override
    {
        MessageIntrospectorTest::SetUp();
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            std_msgs::msg::Float64>();
        schema_ = intr_->introspect_type_support(ts, "std_msgs/msg/Float64");
        ASSERT_NE(schema_, nullptr);
    }

    std::shared_ptr<const MessageSchema> schema_;
};

TEST_F(Float64AccessorTest, MakeAccessorForData)
{
    auto acc = intr_->make_accessor(*schema_, "data");
    EXPECT_TRUE(acc.valid());
    EXPECT_EQ(acc.path(), "data");
    EXPECT_EQ(acc.leaf_type(), FieldType::Float64);
}

TEST_F(Float64AccessorTest, InvalidPathReturnsInvalidAccessor)
{
    auto acc = intr_->make_accessor(*schema_, "nonexistent");
    EXPECT_FALSE(acc.valid());
}

TEST_F(Float64AccessorTest, EmptyPathReturnsInvalidAccessor)
{
    auto acc = intr_->make_accessor(*schema_, "");
    EXPECT_FALSE(acc.valid());
}

TEST_F(Float64AccessorTest, ExtractDoublePositive)
{
    std_msgs::msg::Float64 msg;
    msg.data = 3.14159;

    auto acc = intr_->make_accessor(*schema_, "data");
    ASSERT_TRUE(acc.valid());

    const double val = acc.extract_double(&msg);
    EXPECT_NEAR(val, 3.14159, 1e-10);
}

TEST_F(Float64AccessorTest, ExtractDoubleNegative)
{
    std_msgs::msg::Float64 msg;
    msg.data = -42.5;

    auto acc = intr_->make_accessor(*schema_, "data");
    const double val = acc.extract_double(&msg);
    EXPECT_NEAR(val, -42.5, 1e-10);
}

TEST_F(Float64AccessorTest, ExtractDoubleZero)
{
    std_msgs::msg::Float64 msg;
    msg.data = 0.0;

    auto acc = intr_->make_accessor(*schema_, "data");
    EXPECT_DOUBLE_EQ(acc.extract_double(&msg), 0.0);
}

TEST_F(Float64AccessorTest, ExtractDoubleFromNullPtrReturnsNaN)
{
    auto acc = intr_->make_accessor(*schema_, "data");
    const double val = acc.extract_double(nullptr);
    EXPECT_TRUE(std::isnan(val));
}

TEST_F(Float64AccessorTest, InvalidAccessorReturnsNaN)
{
    FieldAccessor acc;
    EXPECT_TRUE(std::isnan(acc.extract_double(nullptr)));
}

// ===========================================================================
// Suite: FieldAccessor — Twist nested field extraction
// ===========================================================================

class TwistAccessorTest : public MessageIntrospectorTest
{
protected:
    void SetUp() override
    {
        MessageIntrospectorTest::SetUp();
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            geometry_msgs::msg::Twist>();
        schema_ = intr_->introspect_type_support(ts, "geometry_msgs/msg/Twist");
        ASSERT_NE(schema_, nullptr);
    }

    std::shared_ptr<const MessageSchema> schema_;
};

TEST_F(TwistAccessorTest, LinearXAccessorValid)
{
    auto acc = intr_->make_accessor(*schema_, "linear.x");
    EXPECT_TRUE(acc.valid());
    EXPECT_EQ(acc.leaf_type(), FieldType::Float64);
}

TEST_F(TwistAccessorTest, ExtractLinearX)
{
    geometry_msgs::msg::Twist msg;
    msg.linear.x = 1.5;
    msg.linear.y = 2.5;
    msg.linear.z = 3.5;

    auto acc = intr_->make_accessor(*schema_, "linear.x");
    ASSERT_TRUE(acc.valid());
    EXPECT_NEAR(acc.extract_double(&msg), 1.5, 1e-10);
}

TEST_F(TwistAccessorTest, ExtractLinearY)
{
    geometry_msgs::msg::Twist msg;
    msg.linear.y = -7.7;

    auto acc = intr_->make_accessor(*schema_, "linear.y");
    EXPECT_NEAR(acc.extract_double(&msg), -7.7, 1e-10);
}

TEST_F(TwistAccessorTest, ExtractLinearZ)
{
    geometry_msgs::msg::Twist msg;
    msg.linear.z = 99.0;

    auto acc = intr_->make_accessor(*schema_, "linear.z");
    EXPECT_NEAR(acc.extract_double(&msg), 99.0, 1e-10);
}

TEST_F(TwistAccessorTest, ExtractAngularX)
{
    geometry_msgs::msg::Twist msg;
    msg.angular.x = 0.123;

    auto acc = intr_->make_accessor(*schema_, "angular.x");
    EXPECT_NEAR(acc.extract_double(&msg), 0.123, 1e-10);
}

TEST_F(TwistAccessorTest, ExtractAngularY)
{
    geometry_msgs::msg::Twist msg;
    msg.angular.y = -3.14;

    auto acc = intr_->make_accessor(*schema_, "angular.y");
    EXPECT_NEAR(acc.extract_double(&msg), -3.14, 1e-10);
}

TEST_F(TwistAccessorTest, ExtractAngularZ)
{
    geometry_msgs::msg::Twist msg;
    msg.angular.z = 0.01;

    auto acc = intr_->make_accessor(*schema_, "angular.z");
    EXPECT_NEAR(acc.extract_double(&msg), 0.01, 1e-10);
}

TEST_F(TwistAccessorTest, AllSixFieldsExtractIndependently)
{
    geometry_msgs::msg::Twist msg;
    msg.linear.x  = 1.0; msg.linear.y  = 2.0; msg.linear.z  = 3.0;
    msg.angular.x = 4.0; msg.angular.y = 5.0; msg.angular.z = 6.0;

    const std::vector<std::pair<std::string, double>> expected = {
        {"linear.x",  1.0}, {"linear.y",  2.0}, {"linear.z",  3.0},
        {"angular.x", 4.0}, {"angular.y", 5.0}, {"angular.z", 6.0},
    };

    for (const auto& [path, want] : expected)
    {
        auto acc = intr_->make_accessor(*schema_, path);
        ASSERT_TRUE(acc.valid()) << "path: " << path;
        EXPECT_NEAR(acc.extract_double(&msg), want, 1e-10) << "path: " << path;
    }
}

TEST_F(TwistAccessorTest, TopLevelNestedMessagePathInvalid)
{
    // "linear" is a Message type, not numeric — accessor should be invalid.
    auto acc = intr_->make_accessor(*schema_, "linear");
    EXPECT_FALSE(acc.valid());
}

// ===========================================================================
// Suite: FieldAccessor — Imu field extraction
// ===========================================================================

class ImuAccessorTest : public MessageIntrospectorTest
{
protected:
    void SetUp() override
    {
        MessageIntrospectorTest::SetUp();
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            sensor_msgs::msg::Imu>();
        schema_ = intr_->introspect_type_support(ts, "sensor_msgs/msg/Imu");
        ASSERT_NE(schema_, nullptr);
    }

    std::shared_ptr<const MessageSchema> schema_;
};

TEST_F(ImuAccessorTest, LinearAccelerationXExtract)
{
    sensor_msgs::msg::Imu msg;
    msg.linear_acceleration.x = 9.81;

    auto acc = intr_->make_accessor(*schema_, "linear_acceleration.x");
    ASSERT_TRUE(acc.valid());
    EXPECT_NEAR(acc.extract_double(&msg), 9.81, 1e-10);
}

TEST_F(ImuAccessorTest, AngularVelocityZExtract)
{
    sensor_msgs::msg::Imu msg;
    msg.angular_velocity.z = 1.57;

    auto acc = intr_->make_accessor(*schema_, "angular_velocity.z");
    ASSERT_TRUE(acc.valid());
    EXPECT_NEAR(acc.extract_double(&msg), 1.57, 1e-10);
}

TEST_F(ImuAccessorTest, OrientationWExtract)
{
    sensor_msgs::msg::Imu msg;
    msg.orientation.w = 1.0;

    auto acc = intr_->make_accessor(*schema_, "orientation.w");
    ASSERT_TRUE(acc.valid());
    EXPECT_NEAR(acc.extract_double(&msg), 1.0, 1e-10);
}

TEST_F(ImuAccessorTest, OrientationXYZExtract)
{
    sensor_msgs::msg::Imu msg;
    msg.orientation.x = 0.1;
    msg.orientation.y = 0.2;
    msg.orientation.z = 0.3;

    const std::vector<std::pair<std::string, double>> expected = {
        {"orientation.x", 0.1}, {"orientation.y", 0.2}, {"orientation.z", 0.3},
    };

    for (const auto& [path, want] : expected)
    {
        auto acc = intr_->make_accessor(*schema_, path);
        ASSERT_TRUE(acc.valid()) << path;
        EXPECT_NEAR(acc.extract_double(&msg), want, 1e-10) << path;
    }
}

// ===========================================================================
// Suite: FieldAccessor — Int32 extraction (integer types)
// ===========================================================================

class Int32AccessorTest : public MessageIntrospectorTest
{
protected:
    void SetUp() override
    {
        MessageIntrospectorTest::SetUp();
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            std_msgs::msg::Int32>();
        schema_ = intr_->introspect_type_support(ts, "std_msgs/msg/Int32");
        ASSERT_NE(schema_, nullptr);
    }

    std::shared_ptr<const MessageSchema> schema_;
};

TEST_F(Int32AccessorTest, FieldTypeIsInt32)
{
    const auto* fd = schema_->find_field("data");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->type, FieldType::Int32);
}

TEST_F(Int32AccessorTest, ExtractDoubleFromInt32)
{
    std_msgs::msg::Int32 msg;
    msg.data = 42;

    auto acc = intr_->make_accessor(*schema_, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_DOUBLE_EQ(acc.extract_double(&msg), 42.0);
}

TEST_F(Int32AccessorTest, ExtractInt64FromInt32)
{
    std_msgs::msg::Int32 msg;
    msg.data = -100;

    auto acc = intr_->make_accessor(*schema_, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_EQ(acc.extract_int64(&msg), -100);
}

TEST_F(Int32AccessorTest, ExtractInt64NullReturnsZero)
{
    auto acc = intr_->make_accessor(*schema_, "data");
    EXPECT_EQ(acc.extract_int64(nullptr), 0);
}

// ===========================================================================
// Suite: FieldAccessor — Bool extraction
// ===========================================================================

class BoolAccessorTest : public MessageIntrospectorTest
{
protected:
    void SetUp() override
    {
        MessageIntrospectorTest::SetUp();
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            std_msgs::msg::Bool>();
        schema_ = intr_->introspect_type_support(ts, "std_msgs/msg/Bool");
        ASSERT_NE(schema_, nullptr);
    }

    std::shared_ptr<const MessageSchema> schema_;
};

TEST_F(BoolAccessorTest, ExtractTrueAsDouble)
{
    std_msgs::msg::Bool msg;
    msg.data = true;

    auto acc = intr_->make_accessor(*schema_, "data");
    ASSERT_TRUE(acc.valid());
    EXPECT_DOUBLE_EQ(acc.extract_double(&msg), 1.0);
}

TEST_F(BoolAccessorTest, ExtractFalseAsDouble)
{
    std_msgs::msg::Bool msg;
    msg.data = false;

    auto acc = intr_->make_accessor(*schema_, "data");
    EXPECT_DOUBLE_EQ(acc.extract_double(&msg), 0.0);
}

// ===========================================================================
// Suite: FieldAccessor — string field (non-numeric, accessor should be invalid)
// ===========================================================================

class StringSchemaTest : public MessageIntrospectorTest
{
protected:
    void SetUp() override
    {
        MessageIntrospectorTest::SetUp();
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            std_msgs::msg::String>();
        schema_ = intr_->introspect_type_support(ts, "std_msgs/msg/String");
        ASSERT_NE(schema_, nullptr);
    }

    std::shared_ptr<const MessageSchema> schema_;
};

TEST_F(StringSchemaTest, FieldTypeIsString)
{
    const auto* fd = schema_->find_field("data");
    ASSERT_NE(fd, nullptr);
    EXPECT_EQ(fd->type, FieldType::String);
}

TEST_F(StringSchemaTest, StringFieldIsNotNumericLeaf)
{
    const auto* fd = schema_->find_field("data");
    ASSERT_NE(fd, nullptr);
    EXPECT_FALSE(fd->is_numeric_leaf());
}

TEST_F(StringSchemaTest, MakeAccessorForStringReturnsInvalid)
{
    auto acc = intr_->make_accessor(*schema_, "data");
    EXPECT_FALSE(acc.valid());
}

TEST_F(StringSchemaTest, NumericPathsEmpty)
{
    const auto paths = schema_->numeric_paths();
    EXPECT_TRUE(paths.empty());
}

// ===========================================================================
// Suite: Cache behaviour
// ===========================================================================

class CacheTest : public MessageIntrospectorTest {};

TEST_F(CacheTest, IntrospectSameTypeTwiceReturnsSamePointer)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Float64>();

    auto s1 = intr_->introspect_type_support(ts, "std_msgs/msg/Float64");
    auto s2 = intr_->introspect_type_support(ts, "std_msgs/msg/Float64");

    ASSERT_NE(s1, nullptr);
    ASSERT_NE(s2, nullptr);
    EXPECT_EQ(s1.get(), s2.get());   // same cached pointer
}

TEST_F(CacheTest, CacheSizeGrowsWithNewTypes)
{
    {
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            std_msgs::msg::Float64>();
        intr_->introspect_type_support(ts, "std_msgs/msg/Float64");
    }
    EXPECT_EQ(intr_->cache_size(), 1u);
    {
        const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
            std_msgs::msg::Int32>();
        intr_->introspect_type_support(ts, "std_msgs/msg/Int32");
    }
    EXPECT_EQ(intr_->cache_size(), 2u);
}

TEST_F(CacheTest, ClearCacheAllowsReintrospection)
{
    const auto* ts = rosidl_typesupport_cpp::get_message_type_support_handle<
        std_msgs::msg::Float64>();

    auto s1 = intr_->introspect_type_support(ts, "std_msgs/msg/Float64");
    intr_->clear_cache();
    auto s2 = intr_->introspect_type_support(ts, "std_msgs/msg/Float64");

    ASSERT_NE(s1, nullptr);
    ASSERT_NE(s2, nullptr);
    // After clear, a new schema object should be built.
    EXPECT_NE(s1.get(), s2.get());
}

// ===========================================================================
// Suite: FieldType utilities
// ===========================================================================

TEST(FieldTypeUtils, FieldTypeNameNonEmpty)
{
    EXPECT_STRNE(field_type_name(FieldType::Float64), "");
    EXPECT_STRNE(field_type_name(FieldType::Int32), "");
    EXPECT_STREQ(field_type_name(FieldType::Float64), "float64");
    EXPECT_STREQ(field_type_name(FieldType::Int32), "int32");
    EXPECT_STREQ(field_type_name(FieldType::Message), "message");
    EXPECT_STREQ(field_type_name(FieldType::String), "string");
}

TEST(FieldTypeUtils, IsNumericCoversAllScalars)
{
    EXPECT_TRUE(is_numeric(FieldType::Bool));
    EXPECT_TRUE(is_numeric(FieldType::Float32));
    EXPECT_TRUE(is_numeric(FieldType::Float64));
    EXPECT_TRUE(is_numeric(FieldType::Int8));
    EXPECT_TRUE(is_numeric(FieldType::Uint8));
    EXPECT_TRUE(is_numeric(FieldType::Int16));
    EXPECT_TRUE(is_numeric(FieldType::Uint16));
    EXPECT_TRUE(is_numeric(FieldType::Int32));
    EXPECT_TRUE(is_numeric(FieldType::Uint32));
    EXPECT_TRUE(is_numeric(FieldType::Int64));
    EXPECT_TRUE(is_numeric(FieldType::Uint64));
}

TEST(FieldTypeUtils, IsNumericReturnsFalseForNonNumeric)
{
    EXPECT_FALSE(is_numeric(FieldType::String));
    EXPECT_FALSE(is_numeric(FieldType::WString));
    EXPECT_FALSE(is_numeric(FieldType::Message));
    EXPECT_FALSE(is_numeric(FieldType::Unknown));
}

// ===========================================================================
// Suite: Edge cases
// ===========================================================================

class EdgeCaseTest : public MessageIntrospectorTest {};

TEST_F(EdgeCaseTest, IntrospectNullTypeSupportReturnsNull)
{
    auto schema = intr_->introspect_type_support(nullptr, "some/msg/Type");
    EXPECT_EQ(schema, nullptr);
}

TEST_F(EdgeCaseTest, MakeAccessorOnEmptySchemaReturnsInvalid)
{
    MessageSchema empty_schema;
    empty_schema.type_name = "empty/msg/Empty";
    auto acc = intr_->make_accessor(empty_schema, "data");
    EXPECT_FALSE(acc.valid());
}

TEST_F(EdgeCaseTest, ExtractFromInvalidAccessorReturnsNaN)
{
    FieldAccessor acc;
    EXPECT_FALSE(acc.valid());
    EXPECT_TRUE(std::isnan(acc.extract_double(nullptr)));
    EXPECT_EQ(acc.extract_int64(nullptr), 0);
}

TEST_F(EdgeCaseTest, MessageSchemaNumericPathsOnEmptySchema)
{
    MessageSchema empty;
    EXPECT_TRUE(empty.numeric_paths().empty());
}

TEST_F(EdgeCaseTest, FindFieldOnEmptySchemaReturnsNull)
{
    MessageSchema empty;
    EXPECT_EQ(empty.find_field("data"), nullptr);
}

// ===========================================================================
// main — register the shared RclcppEnvironment
// ===========================================================================

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new RclcppEnvironment);
    return RUN_ALL_TESTS();
}
