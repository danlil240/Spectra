// Tests for the ImGui-free PX4 panel helpers: Parameters-table sorting and
// stable widget labels for live telemetry rows.

#include <gtest/gtest.h>

#include "ui/px4_panel_helpers.hpp"

#include <string>
#include <vector>

using namespace spectra::adapters::px4;

namespace
{

ULogParameter make_float(const std::string& key, float v)
{
    ULogParameter p;
    p.key         = key;
    p.is_float    = true;
    p.value_float = v;
    return p;
}

ULogParameter make_int(const std::string& key, int32_t v)
{
    ULogParameter p;
    p.key       = key;
    p.is_float  = false;
    p.value_int = v;
    return p;
}

std::vector<std::string> keys_in_order(const std::vector<ULogParameter>& params,
                                       const std::vector<size_t>&        order)
{
    std::vector<std::string> out;
    for (size_t i : order)
        out.push_back(params[i].key);
    return out;
}

// Unsorted on purpose: file order differs from every sort order tested.
std::vector<ULogParameter> sample_params()
{
    return {
        make_float("MC_ROLL_P", 6.5f),
        make_int("SYS_AUTOSTART", 4001),
        make_float("BAT_LOW_THR", 0.15f),
        make_int("COM_ARM_WO_GPS", 1),
        make_float("MPC_XY_VEL_MAX", 12.0f),
    };
}

}   // namespace

TEST(Px4PanelHelpers, NoSortKeysKeepsFileOrder)
{
    auto params = sample_params();
    auto order  = sorted_parameter_indices(params, {});
    EXPECT_EQ(order, (std::vector<size_t>{0, 1, 2, 3, 4}));
}

TEST(Px4PanelHelpers, SortByNameAscendingAndDescending)
{
    auto         params = sample_params();
    ParamSortKey asc{ParamSortColumn::Name, true};
    EXPECT_EQ(keys_in_order(params, sorted_parameter_indices(params, {&asc, 1})),
              (std::vector<std::string>{"BAT_LOW_THR",
                                        "COM_ARM_WO_GPS",
                                        "MC_ROLL_P",
                                        "MPC_XY_VEL_MAX",
                                        "SYS_AUTOSTART"}));

    ParamSortKey desc{ParamSortColumn::Name, false};
    EXPECT_EQ(keys_in_order(params, sorted_parameter_indices(params, {&desc, 1})),
              (std::vector<std::string>{"SYS_AUTOSTART",
                                        "MPC_XY_VEL_MAX",
                                        "MC_ROLL_P",
                                        "COM_ARM_WO_GPS",
                                        "BAT_LOW_THR"}));
}

TEST(Px4PanelHelpers, SortByValueIsNumericAcrossTypes)
{
    auto         params = sample_params();
    ParamSortKey asc{ParamSortColumn::Value, true};
    // 0.15 < 1 < 6.5 < 12 < 4001 — a string compare would put "12.0" before "6.5".
    EXPECT_EQ(keys_in_order(params, sorted_parameter_indices(params, {&asc, 1})),
              (std::vector<std::string>{"BAT_LOW_THR",
                                        "COM_ARM_WO_GPS",
                                        "MC_ROLL_P",
                                        "MPC_XY_VEL_MAX",
                                        "SYS_AUTOSTART"}));

    ParamSortKey desc{ParamSortColumn::Value, false};
    EXPECT_EQ(keys_in_order(params, sorted_parameter_indices(params, {&desc, 1})).front(),
              "SYS_AUTOSTART");
}

TEST(Px4PanelHelpers, SortByValueHandlesNegatives)
{
    std::vector<ULogParameter> params = {make_int("A", 2),
                                         make_float("B", -3.5f),
                                         make_int("C", -1)};
    ParamSortKey               asc{ParamSortColumn::Value, true};
    EXPECT_EQ(keys_in_order(params, sorted_parameter_indices(params, {&asc, 1})),
              (std::vector<std::string>{"B", "C", "A"}));
}

TEST(Px4PanelHelpers, SortByTypeIsStableAndSupportsSecondaryKey)
{
    auto         params = sample_params();
    ParamSortKey type_only{ParamSortColumn::Type, true};
    // "float" < "int32"; ties keep file order.
    EXPECT_EQ(keys_in_order(params, sorted_parameter_indices(params, {&type_only, 1})),
              (std::vector<std::string>{"MC_ROLL_P",
                                        "BAT_LOW_THR",
                                        "MPC_XY_VEL_MAX",
                                        "SYS_AUTOSTART",
                                        "COM_ARM_WO_GPS"}));

    std::vector<ParamSortKey> type_then_name = {{ParamSortColumn::Type, false},
                                                {ParamSortColumn::Name, true}};
    EXPECT_EQ(keys_in_order(params, sorted_parameter_indices(params, type_then_name)),
              (std::vector<std::string>{"COM_ARM_WO_GPS",
                                        "SYS_AUTOSTART",
                                        "BAT_LOW_THR",
                                        "MC_ROLL_P",
                                        "MPC_XY_VEL_MAX"}));
}

TEST(Px4PanelHelpers, SortEmptyParameterSet)
{
    std::vector<ULogParameter> params;
    ParamSortKey               asc{ParamSortColumn::Name, true};
    EXPECT_TRUE(sorted_parameter_indices(params, {&asc, 1}).empty());
}

TEST(Px4PanelHelpers, ParameterTypeAndNumericValue)
{
    EXPECT_STREQ(parameter_type_name(make_float("X", 1.0f)), "float");
    EXPECT_STREQ(parameter_type_name(make_int("X", 1)), "int32");
    EXPECT_DOUBLE_EQ(parameter_numeric_value(make_int("X", -7)), -7.0);
    EXPECT_DOUBLE_EQ(parameter_numeric_value(make_float("X", 0.5f)), 0.5);
}

// ImGui hashes only the text after "###"; return that part.
static std::string id_part(const std::string& label)
{
    auto pos = label.find("###");
    return pos == std::string::npos ? label : label.substr(pos + 3);
}

TEST(Px4PanelHelpers, LiveFieldLabelIdIsIndependentOfValue)
{
    const std::string a = live_field_label("roll", 0.1234);
    const std::string b = live_field_label("roll", -98.7);
    EXPECT_NE(a, b);                     // display text changes...
    EXPECT_EQ(id_part(a), id_part(b));   // ...but the ImGui ID does not.
    EXPECT_EQ(id_part(a), "roll");
    EXPECT_EQ(a.substr(0, a.find("###")), "roll = 0.1234");
    EXPECT_NE(id_part(live_field_label("pitch", 0.1234)), id_part(a));
}

TEST(Px4PanelHelpers, LiveChannelLabelIdIsIndependentOfFieldCount)
{
    const std::string a = live_channel_label("ATTITUDE", 3);
    const std::string b = live_channel_label("ATTITUDE", 7);
    EXPECT_EQ(id_part(a), id_part(b));
    EXPECT_EQ(id_part(a), "ATTITUDE");
    EXPECT_EQ(b.substr(0, b.find("###")), "ATTITUDE (7 fields)");
}

TEST(Px4PanelHelpers, StableLabelFormat)
{
    EXPECT_EQ(stable_label("shown", "id"), "shown###id");
}
