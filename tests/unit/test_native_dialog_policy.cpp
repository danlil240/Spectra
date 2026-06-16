#include "ui/native_dialog_policy.hpp"

#include <gtest/gtest.h>

#include <cstring>

namespace
{

class NativeDialogPolicyTest : public ::testing::Test
{
   protected:
    void SetUp() override
    {
        saved_enabled_ = spectra::native_dialogs_enabled();
    }

    void TearDown() override { spectra::set_native_dialogs_enabled(saved_enabled_); }

    bool saved_enabled_ = true;
};

TEST_F(NativeDialogPolicyTest, StripsNoNativeDialogsFlag)
{
    char arg0[] = "spectra";
    char arg1[] = "--no-native-dialogs";
    char arg2[] = "--help";
    char* argv[] = {arg0, arg1, arg2, nullptr};
    int   argc   = 3;

    spectra::set_native_dialogs_enabled(true);
    spectra::init_native_dialog_policy(argc, argv);

    EXPECT_EQ(argc, 2);
    EXPECT_STREQ(argv[1], "--help");
    EXPECT_FALSE(spectra::native_dialogs_enabled());
}

TEST_F(NativeDialogPolicyTest, EnvVarDisablesDialogs)
{
    spectra::set_native_dialogs_enabled(true);
    setenv("SPECTRA_NO_NATIVE_DIALOGS", "1", 1);

    char arg0[] = "spectra";
    char* argv[] = {arg0, nullptr};
    int   argc   = 1;
    spectra::init_native_dialog_policy(argc, argv);

    EXPECT_FALSE(spectra::native_dialogs_enabled());
    unsetenv("SPECTRA_NO_NATIVE_DIALOGS");
}

}   // namespace
