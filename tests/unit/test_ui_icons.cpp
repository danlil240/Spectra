#include <gtest/gtest.h>

#include "ui/icons.hpp"

using namespace spectra::ui;

TEST(IconFont, AllIconsListUsesEnumValues)
{
    const auto& icons = IconFont::instance().get_all_icons();
    ASSERT_FALSE(icons.empty());

    const auto first = static_cast<uint16_t>(icons.front());
    const auto last = static_cast<uint16_t>(icons.back());
    const auto enum_first = static_cast<uint16_t>(Icon::ChartLine);
    const auto enum_last = static_cast<uint16_t>(Icon::Last);

    EXPECT_GE(first, enum_first);
    EXPECT_LT(first, enum_last);
    EXPECT_GE(last, enum_first);
    EXPECT_LT(last, enum_last);

    for (Icon icon : icons)
    {
        const auto value = static_cast<uint16_t>(icon);
        EXPECT_GE(value, enum_first);
        EXPECT_LT(value, enum_last);
    }
}
