#include <gtest/gtest.h>

#include "ui/knob_manager.hpp"

using namespace spectra;

// ─── Construction ────────────────────────────────────────────────────────────

TEST(KnobManagerConstruction, DefaultEmpty)
{
    KnobManager mgr;
    EXPECT_TRUE(mgr.empty());
    EXPECT_EQ(mgr.count(), 0u);
}

TEST(KnobManagerConstruction, VisibleByDefault)
{
    KnobManager mgr;
    EXPECT_TRUE(mgr.is_visible());
    EXPECT_FALSE(mgr.is_collapsed());
}

// ─── Add Float ───────────────────────────────────────────────────────────────

TEST(KnobManagerFloat, AddFloat)
{
    KnobManager mgr;
    auto& k = mgr.add_float("Frequency", 1.0f, 0.1f, 10.0f);
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_FALSE(mgr.empty());
    EXPECT_EQ(k.name, "Frequency");
    EXPECT_EQ(k.type, KnobType::Float);
    EXPECT_FLOAT_EQ(k.value, 1.0f);
    EXPECT_FLOAT_EQ(k.min_val, 0.1f);
    EXPECT_FLOAT_EQ(k.max_val, 10.0f);
    EXPECT_FLOAT_EQ(k.step, 0.0f);
}

TEST(KnobManagerFloat, AddFloatWithStep)
{
    KnobManager mgr;
    auto& k = mgr.add_float("Gain", 5.0f, 0.0f, 20.0f, 0.5f);
    EXPECT_FLOAT_EQ(k.step, 0.5f);
}

TEST(KnobManagerFloat, FloatCallback)
{
    KnobManager mgr;
    float captured = 0.0f;
    mgr.add_float("X", 1.0f, 0.0f, 10.0f, 0.0f, [&](float v) { captured = v; });
    EXPECT_TRUE(mgr.set_value("X", 5.0f));
    EXPECT_FLOAT_EQ(captured, 5.0f);
}

// ─── Add Int ─────────────────────────────────────────────────────────────────

TEST(KnobManagerInt, AddInt)
{
    KnobManager mgr;
    auto& k = mgr.add_int("Harmonics", 3, 1, 10);
    EXPECT_EQ(k.type, KnobType::Int);
    EXPECT_EQ(k.int_value(), 3);
    EXPECT_FLOAT_EQ(k.min_val, 1.0f);
    EXPECT_FLOAT_EQ(k.max_val, 10.0f);
    EXPECT_FLOAT_EQ(k.step, 1.0f);
}

TEST(KnobManagerInt, IntCallback)
{
    KnobManager mgr;
    int captured = 0;
    mgr.add_int("N", 2, 0, 100, [&](float v) { captured = static_cast<int>(v); });
    mgr.set_value("N", 42.0f);
    EXPECT_EQ(captured, 42);
}

// ─── Add Bool ────────────────────────────────────────────────────────────────

TEST(KnobManagerBool, AddBoolTrue)
{
    KnobManager mgr;
    auto& k = mgr.add_bool("Show Grid", true);
    EXPECT_EQ(k.type, KnobType::Bool);
    EXPECT_TRUE(k.bool_value());
    EXPECT_FLOAT_EQ(k.value, 1.0f);
}

TEST(KnobManagerBool, AddBoolFalse)
{
    KnobManager mgr;
    auto& k = mgr.add_bool("Muted", false);
    EXPECT_FALSE(k.bool_value());
    EXPECT_FLOAT_EQ(k.value, 0.0f);
}

TEST(KnobManagerBool, BoolCallback)
{
    KnobManager mgr;
    bool captured = false;
    mgr.add_bool("Toggle", false, [&](float v) { captured = v >= 0.5f; });
    mgr.set_value("Toggle", 1.0f);
    EXPECT_TRUE(captured);
}

// ─── Add Choice ──────────────────────────────────────────────────────────────

TEST(KnobManagerChoice, AddChoice)
{
    KnobManager mgr;
    auto& k = mgr.add_choice("Waveform", {"Sine", "Square", "Triangle"}, 1);
    EXPECT_EQ(k.type, KnobType::Choice);
    EXPECT_EQ(k.choice_index(), 1);
    EXPECT_EQ(k.choices.size(), 3u);
    EXPECT_EQ(k.choices[0], "Sine");
    EXPECT_EQ(k.choices[2], "Triangle");
    EXPECT_FLOAT_EQ(k.max_val, 2.0f);
}

TEST(KnobManagerChoice, ChoiceCallback)
{
    KnobManager mgr;
    int captured = -1;
    mgr.add_choice("Mode", {"A", "B", "C"}, 0, [&](float v) { captured = static_cast<int>(v); });
    mgr.set_value("Mode", 2.0f);
    EXPECT_EQ(captured, 2);
}

TEST(KnobManagerChoice, EmptyChoices)
{
    KnobManager mgr;
    auto& k = mgr.add_choice("Empty", {});
    EXPECT_EQ(k.choices.size(), 0u);
    EXPECT_FLOAT_EQ(k.max_val, 0.0f);
}

// ─── Find ────────────────────────────────────────────────────────────────────

TEST(KnobManagerFind, FindExisting)
{
    KnobManager mgr;
    mgr.add_float("Alpha", 0.5f, 0.0f, 1.0f);
    auto* k = mgr.find("Alpha");
    ASSERT_NE(k, nullptr);
    EXPECT_EQ(k->name, "Alpha");
}

TEST(KnobManagerFind, FindNonexistent)
{
    KnobManager mgr;
    mgr.add_float("Alpha", 0.5f, 0.0f, 1.0f);
    auto* k = mgr.find("Beta");
    EXPECT_EQ(k, nullptr);
}

TEST(KnobManagerFind, FindConst)
{
    KnobManager mgr;
    mgr.add_float("X", 1.0f, 0.0f, 10.0f);
    const auto& cmgr = mgr;
    const auto* k = cmgr.find("X");
    ASSERT_NE(k, nullptr);
    EXPECT_FLOAT_EQ(k->value, 1.0f);
}

// ─── Value ───────────────────────────────────────────────────────────────────

TEST(KnobManagerValue, GetValue)
{
    KnobManager mgr;
    mgr.add_float("X", 3.14f, 0.0f, 10.0f);
    EXPECT_FLOAT_EQ(mgr.value("X"), 3.14f);
}

TEST(KnobManagerValue, GetValueDefault)
{
    KnobManager mgr;
    EXPECT_FLOAT_EQ(mgr.value("Missing", -1.0f), -1.0f);
}

TEST(KnobManagerValue, SetValueClampsMin)
{
    KnobManager mgr;
    mgr.add_float("X", 5.0f, 1.0f, 10.0f);
    mgr.set_value("X", -100.0f);
    EXPECT_FLOAT_EQ(mgr.value("X"), 1.0f);
}

TEST(KnobManagerValue, SetValueClampsMax)
{
    KnobManager mgr;
    mgr.add_float("X", 5.0f, 1.0f, 10.0f);
    mgr.set_value("X", 999.0f);
    EXPECT_FLOAT_EQ(mgr.value("X"), 10.0f);
}

TEST(KnobManagerValue, SetValueNotFound)
{
    KnobManager mgr;
    EXPECT_FALSE(mgr.set_value("Missing", 1.0f));
}

// ─── Remove ──────────────────────────────────────────────────────────────────

TEST(KnobManagerRemove, RemoveExisting)
{
    KnobManager mgr;
    mgr.add_float("A", 1.0f, 0.0f, 10.0f);
    mgr.add_float("B", 2.0f, 0.0f, 10.0f);
    EXPECT_EQ(mgr.count(), 2u);
    EXPECT_TRUE(mgr.remove("A"));
    EXPECT_EQ(mgr.count(), 1u);
    EXPECT_EQ(mgr.find("A"), nullptr);
    EXPECT_NE(mgr.find("B"), nullptr);
}

TEST(KnobManagerRemove, RemoveNonexistent)
{
    KnobManager mgr;
    EXPECT_FALSE(mgr.remove("Missing"));
}

TEST(KnobManagerRemove, Clear)
{
    KnobManager mgr;
    mgr.add_float("A", 1.0f, 0.0f, 10.0f);
    mgr.add_int("B", 5, 0, 10);
    mgr.add_bool("C", true);
    EXPECT_EQ(mgr.count(), 3u);
    mgr.clear();
    EXPECT_TRUE(mgr.empty());
    EXPECT_EQ(mgr.count(), 0u);
}

// ─── Global Callback ─────────────────────────────────────────────────────────

TEST(KnobManagerCallback, OnAnyChange)
{
    KnobManager mgr;
    int call_count = 0;
    mgr.set_on_any_change([&]() { call_count++; });
    mgr.add_float("X", 1.0f, 0.0f, 10.0f);
    // set_value fires the global callback internally
    mgr.set_value("X", 5.0f);
    EXPECT_EQ(call_count, 1);
    // notify_any_changed also fires it (used by ImGui draw code)
    mgr.notify_any_changed();
    EXPECT_EQ(call_count, 2);
}

TEST(KnobManagerCallback, BothCallbacksFire)
{
    KnobManager mgr;
    float per_knob_val = 0.0f;
    int global_count = 0;
    mgr.add_float("X", 1.0f, 0.0f, 10.0f, 0.0f, [&](float v) { per_knob_val = v; });
    mgr.set_on_any_change([&]() { global_count++; });

    // set_value fires BOTH per-knob and global callbacks
    mgr.set_value("X", 7.0f);
    EXPECT_FLOAT_EQ(per_knob_val, 7.0f);
    EXPECT_EQ(global_count, 1);
}

// ─── Panel State ─────────────────────────────────────────────────────────────

TEST(KnobManagerPanel, Visibility)
{
    KnobManager mgr;
    EXPECT_TRUE(mgr.is_visible());
    mgr.set_visible(false);
    EXPECT_FALSE(mgr.is_visible());
}

TEST(KnobManagerPanel, Collapsed)
{
    KnobManager mgr;
    EXPECT_FALSE(mgr.is_collapsed());
    mgr.set_collapsed(true);
    EXPECT_TRUE(mgr.is_collapsed());
}

// ─── Multiple Knobs ──────────────────────────────────────────────────────────

TEST(KnobManagerMultiple, MixedTypes)
{
    KnobManager mgr;
    mgr.add_float("Freq", 1.0f, 0.0f, 10.0f);
    mgr.add_int("Harmonics", 3, 1, 10);
    mgr.add_bool("Grid", true);
    mgr.add_choice("Wave", {"Sine", "Square"});
    EXPECT_EQ(mgr.count(), 4u);
    EXPECT_EQ(mgr.find("Freq")->type, KnobType::Float);
    EXPECT_EQ(mgr.find("Harmonics")->type, KnobType::Int);
    EXPECT_EQ(mgr.find("Grid")->type, KnobType::Bool);
    EXPECT_EQ(mgr.find("Wave")->type, KnobType::Choice);
}

TEST(KnobManagerMultiple, KnobsAccessor)
{
    KnobManager mgr;
    mgr.add_float("A", 1.0f, 0.0f, 5.0f);
    mgr.add_float("B", 2.0f, 0.0f, 5.0f);
    auto& knobs = mgr.knobs();
    EXPECT_EQ(knobs.size(), 2u);
    EXPECT_EQ(knobs[0].name, "A");
    EXPECT_EQ(knobs[1].name, "B");
}

// ─── Edge Cases ──────────────────────────────────────────────────────────────

TEST(KnobManagerEdge, SetSameValueNoCallback)
{
    KnobManager mgr;
    int call_count = 0;
    mgr.add_float("X", 5.0f, 0.0f, 10.0f, 0.0f, [&](float) { call_count++; });
    mgr.set_value("X", 5.0f);  // Same value — should not fire
    EXPECT_EQ(call_count, 0);
}

TEST(KnobManagerEdge, IntAccessorsOnFloat)
{
    KnobManager mgr;
    auto& k = mgr.add_float("X", 3.7f, 0.0f, 10.0f);
    EXPECT_EQ(k.int_value(), 3);  // Truncates
}

TEST(KnobManagerEdge, BoolAccessorOnFloat)
{
    KnobManager mgr;
    auto& k = mgr.add_float("X", 0.3f, 0.0f, 1.0f);
    EXPECT_FALSE(k.bool_value());  // < 0.5
    k.value = 0.7f;
    EXPECT_TRUE(k.bool_value());  // >= 0.5
}

TEST(KnobManagerEdge, ChoiceClampedIndex)
{
    KnobManager mgr;
    mgr.add_choice("Mode", {"A", "B", "C"}, 0);
    mgr.set_value("Mode", 10.0f);  // Clamped to max=2
    EXPECT_FLOAT_EQ(mgr.value("Mode"), 2.0f);
}
