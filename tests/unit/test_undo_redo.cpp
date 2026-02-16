#include <gtest/gtest.h>
#include <string>

#include "ui/undo_manager.hpp"

using namespace plotix;

// ─── Initial state ───────────────────────────────────────────────────────────

TEST(UndoManager, InitiallyEmpty)
{
    UndoManager mgr;
    EXPECT_FALSE(mgr.can_undo());
    EXPECT_FALSE(mgr.can_redo());
    EXPECT_EQ(mgr.undo_count(), 0u);
    EXPECT_EQ(mgr.redo_count(), 0u);
}

// ─── Push / Undo / Redo ──────────────────────────────────────────────────────

TEST(UndoManager, PushMakesUndoAvailable)
{
    UndoManager mgr;
    mgr.push({"Test", []() {}, []() {}});
    EXPECT_TRUE(mgr.can_undo());
    EXPECT_EQ(mgr.undo_count(), 1u);
}

TEST(UndoManager, UndoCallsUndoFn)
{
    UndoManager mgr;
    int value = 10;
    mgr.push({"Set to 10", [&]() { value = 0; }, [&]() { value = 10; }});

    EXPECT_TRUE(mgr.undo());
    EXPECT_EQ(value, 0);
}

TEST(UndoManager, RedoCallsRedoFn)
{
    UndoManager mgr;
    int value = 10;
    mgr.push({"Set to 10", [&]() { value = 0; }, [&]() { value = 10; }});

    mgr.undo();
    EXPECT_EQ(value, 0);

    EXPECT_TRUE(mgr.redo());
    EXPECT_EQ(value, 10);
}

TEST(UndoManager, UndoEmptyReturnsFalse)
{
    UndoManager mgr;
    EXPECT_FALSE(mgr.undo());
}

TEST(UndoManager, RedoEmptyReturnsFalse)
{
    UndoManager mgr;
    EXPECT_FALSE(mgr.redo());
}

TEST(UndoManager, UndoMakesRedoAvailable)
{
    UndoManager mgr;
    mgr.push({"Test", []() {}, []() {}});
    EXPECT_FALSE(mgr.can_redo());

    mgr.undo();
    EXPECT_TRUE(mgr.can_redo());
}

TEST(UndoManager, NewPushClearsRedoStack)
{
    UndoManager mgr;
    int value = 0;
    mgr.push({"A", [&]() { value = 0; }, [&]() { value = 1; }});
    mgr.push({"B", [&]() { value = 1; }, [&]() { value = 2; }});

    mgr.undo();  // Undo B
    EXPECT_TRUE(mgr.can_redo());

    mgr.push({"C", [&]() { value = 1; }, [&]() { value = 3; }});
    EXPECT_FALSE(mgr.can_redo());  // Redo stack cleared
}

// ─── Multiple undo/redo ──────────────────────────────────────────────────────

TEST(UndoManager, MultipleUndoRedo)
{
    UndoManager mgr;
    int value = 0;

    mgr.push({"Set 1", [&]() { value = 0; }, [&]() { value = 1; }});
    value = 1;
    mgr.push({"Set 2", [&]() { value = 1; }, [&]() { value = 2; }});
    value = 2;
    mgr.push({"Set 3", [&]() { value = 2; }, [&]() { value = 3; }});
    value = 3;

    EXPECT_EQ(mgr.undo_count(), 3u);

    mgr.undo();  // value = 2
    EXPECT_EQ(value, 2);
    mgr.undo();  // value = 1
    EXPECT_EQ(value, 1);
    mgr.undo();  // value = 0
    EXPECT_EQ(value, 0);

    EXPECT_FALSE(mgr.can_undo());
    EXPECT_EQ(mgr.redo_count(), 3u);

    mgr.redo();  // value = 1
    EXPECT_EQ(value, 1);
    mgr.redo();  // value = 2
    EXPECT_EQ(value, 2);
    mgr.redo();  // value = 3
    EXPECT_EQ(value, 3);

    EXPECT_FALSE(mgr.can_redo());
}

// ─── Descriptions ────────────────────────────────────────────────────────────

TEST(UndoManager, UndoDescription)
{
    UndoManager mgr;
    mgr.push({"Change color", []() {}, []() {}});
    EXPECT_EQ(mgr.undo_description(), "Change color");
}

TEST(UndoManager, RedoDescription)
{
    UndoManager mgr;
    mgr.push({"Change color", []() {}, []() {}});
    mgr.undo();
    EXPECT_EQ(mgr.redo_description(), "Change color");
}

TEST(UndoManager, EmptyDescriptions)
{
    UndoManager mgr;
    EXPECT_EQ(mgr.undo_description(), "");
    EXPECT_EQ(mgr.redo_description(), "");
}

// ─── push_value convenience ──────────────────────────────────────────────────

TEST(UndoManager, PushValueUndoRedo)
{
    UndoManager mgr;
    float line_width = 2.0f;

    mgr.push_value<float>(
        "Change line width", 2.0f, 4.0f, [&line_width](const float& v) { line_width = v; });
    line_width = 4.0f;

    mgr.undo();
    EXPECT_FLOAT_EQ(line_width, 2.0f);

    mgr.redo();
    EXPECT_FLOAT_EQ(line_width, 4.0f);
}

TEST(UndoManager, PushValueString)
{
    UndoManager mgr;
    std::string label = "new label";

    mgr.push_value<std::string>(
        "Change label", "old label", "new label", [&label](const std::string& v) { label = v; });

    mgr.undo();
    EXPECT_EQ(label, "old label");

    mgr.redo();
    EXPECT_EQ(label, "new label");
}

// ─── Stack size limit ────────────────────────────────────────────────────────

TEST(UndoManager, StackSizeLimit)
{
    UndoManager mgr;

    for (size_t i = 0; i < UndoManager::MAX_STACK_SIZE + 20; ++i)
    {
        mgr.push({"Action " + std::to_string(i), []() {}, []() {}});
    }

    EXPECT_EQ(mgr.undo_count(), UndoManager::MAX_STACK_SIZE);
}

// ─── Clear ───────────────────────────────────────────────────────────────────

TEST(UndoManager, ClearRemovesAll)
{
    UndoManager mgr;
    mgr.push({"A", []() {}, []() {}});
    mgr.push({"B", []() {}, []() {}});
    mgr.undo();

    EXPECT_TRUE(mgr.can_undo());
    EXPECT_TRUE(mgr.can_redo());

    mgr.clear();
    EXPECT_FALSE(mgr.can_undo());
    EXPECT_FALSE(mgr.can_redo());
    EXPECT_EQ(mgr.undo_count(), 0u);
    EXPECT_EQ(mgr.redo_count(), 0u);
}

// ─── Grouping ────────────────────────────────────────────────────────────────

TEST(UndoManager, GroupCombinesActions)
{
    UndoManager mgr;
    int a = 0, b = 0;

    mgr.begin_group("Multi-change");
    mgr.push({"Set A", [&]() { a = 0; }, [&]() { a = 1; }});
    mgr.push({"Set B", [&]() { b = 0; }, [&]() { b = 1; }});
    mgr.end_group();

    a = 1;
    b = 1;

    EXPECT_EQ(mgr.undo_count(), 1u);  // Single grouped action

    mgr.undo();
    EXPECT_EQ(a, 0);
    EXPECT_EQ(b, 0);

    mgr.redo();
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);
}

TEST(UndoManager, GroupUndoReverseOrder)
{
    UndoManager mgr;
    std::vector<int> order;

    mgr.begin_group("Ordered");
    mgr.push({"1", [&]() { order.push_back(1); }, [&]() {}});
    mgr.push({"2", [&]() { order.push_back(2); }, [&]() {}});
    mgr.push({"3", [&]() { order.push_back(3); }, [&]() {}});
    mgr.end_group();

    mgr.undo();

    // Undo should execute in reverse order
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 3);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 1);
}

TEST(UndoManager, EmptyGroupIsNoOp)
{
    UndoManager mgr;
    mgr.begin_group("Empty");
    mgr.end_group();
    EXPECT_EQ(mgr.undo_count(), 0u);
}

TEST(UndoManager, InGroupQuery)
{
    UndoManager mgr;
    EXPECT_FALSE(mgr.in_group());

    mgr.begin_group("Test");
    EXPECT_TRUE(mgr.in_group());

    mgr.end_group();
    EXPECT_FALSE(mgr.in_group());
}

TEST(UndoManager, GroupDescription)
{
    UndoManager mgr;
    mgr.begin_group("Batch Edit");
    mgr.push({"A", []() {}, []() {}});
    mgr.push({"B", []() {}, []() {}});
    mgr.end_group();

    EXPECT_EQ(mgr.undo_description(), "Batch Edit");
}

// ─── Null function safety ────────────────────────────────────────────────────

TEST(UndoManager, NullUndoFnSafe)
{
    UndoManager mgr;
    mgr.push({"Test", nullptr, []() {}});
    EXPECT_TRUE(mgr.undo());  // Should not crash
}

TEST(UndoManager, NullRedoFnSafe)
{
    UndoManager mgr;
    mgr.push({"Test", []() {}, nullptr});
    mgr.undo();
    EXPECT_TRUE(mgr.redo());  // Should not crash
}
