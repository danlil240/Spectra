#pragma once

namespace spectra
{

/// Returns false when native OS file dialogs must not be shown (automation / fuzz).
/// A native dialog is modal and blocks the main loop, so an agent driving the app
/// (MCP, QA agent, scripted captures) would hang on it.
bool native_dialogs_enabled();

/// True when SPECTRA_NO_NATIVE_DIALOGS or SPECTRA_AUTOMATION is set to a truthy value.
bool native_dialogs_disabled_by_env();

/// Enable or suppress native file dialogs for the process lifetime.
void set_native_dialogs_enabled(bool enabled);

/// Read SPECTRA_NO_NATIVE_DIALOGS / SPECTRA_AUTOMATION and strip --no-native-dialogs from argv.
void init_native_dialog_policy(int& argc, char** argv);

}   // namespace spectra
