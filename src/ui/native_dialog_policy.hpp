#pragma once

namespace spectra
{

/// Returns false when native OS file dialogs must not be shown (automation / fuzz).
bool native_dialogs_enabled();

/// Enable or suppress native file dialogs for the process lifetime.
void set_native_dialogs_enabled(bool enabled);

/// Read SPECTRA_NO_NATIVE_DIALOGS / SPECTRA_AUTOMATION and strip --no-native-dialogs from argv.
void init_native_dialog_policy(int& argc, char** argv);

}   // namespace spectra
