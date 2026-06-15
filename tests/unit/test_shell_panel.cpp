#ifdef SPECTRA_USE_IMGUI
    #include <gtest/gtest.h>
    #include "ui/shell/panel.hpp"

using namespace spectra::ui;
using namespace spectra::ui::shell;

TEST(ShellPanel, CallbackPanelReportsInfo)
{
    PanelInfo info;
    info.id              = "test.panel";
    info.title           = "Test Panel";
    info.category        = "Testing";
    info.icon            = Icon::ChartArea;
    info.slot            = DockSlot::Left;
    info.default_visible = true;

    CallbackPanel panel(info, [](bool*) {});

    EXPECT_EQ(panel.id(), "test.panel");
    EXPECT_EQ(panel.title(), "Test Panel");
    EXPECT_EQ(panel.category(), "Testing");
    EXPECT_EQ(panel.icon(), Icon::ChartArea);
    EXPECT_EQ(panel.default_slot(), DockSlot::Left);
    EXPECT_TRUE(panel.closable());
    EXPECT_TRUE(panel.visible());
}

TEST(ShellPanel, SetVisibleToggles)
{
    PanelInfo info;
    info.default_visible = false;

    CallbackPanel panel(info, [](bool*) {});

    EXPECT_FALSE(panel.visible());
    panel.set_visible(true);
    EXPECT_TRUE(panel.visible());

    bool* ptr = panel.visible_ptr();
    ASSERT_NE(ptr, nullptr);
    *ptr = false;
    EXPECT_FALSE(panel.visible());
    *ptr = true;
    EXPECT_TRUE(panel.visible());
}

TEST(ShellPanel, ClosablePanelForwardsNonNullPOpen)
{
    PanelInfo info;
    info.closable        = true;
    info.default_visible = true;

    bool*         received = nullptr;
    CallbackPanel panel(info, [&](bool* p_open) { received = p_open; });

    panel.draw();

    EXPECT_NE(received, nullptr);
    EXPECT_EQ(received, panel.visible_ptr());
}

TEST(ShellPanel, ClosablePanelClosesWhenDrawFnClearsPOpen)
{
    PanelInfo info;
    info.closable        = true;
    info.default_visible = true;

    CallbackPanel panel(info,
                        [](bool* p_open)
                        {
                            if (p_open)
                                *p_open = false;
                        });

    ASSERT_TRUE(panel.visible());
    panel.draw();
    EXPECT_FALSE(panel.visible());
}

TEST(ShellPanel, NonClosablePanelForwardsNullPOpen)
{
    PanelInfo info;
    info.closable = false;

    bool*         received = reinterpret_cast<bool*>(1);
    CallbackPanel panel(info, [&](bool* p_open) { received = p_open; });

    panel.draw();

    EXPECT_EQ(received, nullptr);
}
#else
    #include <gtest/gtest.h>
TEST(ShellPanel, SkippedWithoutImGui)
{
    SUCCEED();
}
#endif
