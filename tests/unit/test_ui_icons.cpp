#include <gtest/gtest.h>

#include "ui/theme/icons.hpp"

using namespace spectra::ui;

TEST(IconFont, AllIconsListUsesEnumValues)
{
    const auto& icons = IconFont::instance().get_all_icons();
    ASSERT_FALSE(icons.empty());

    // FA6 Solid codepoints are scattered across U+E000-U+E5FF and U+F000-U+F8FF.
    // Verify each icon is a valid non-zero codepoint below the Last sentinel.
    const auto enum_last = static_cast<uint16_t>(Icon::Last);

    for (Icon icon : icons)
    {
        const auto value = static_cast<uint16_t>(icon);
        EXPECT_GT(value, 0u);
        EXPECT_LT(value, enum_last);
    }
}
