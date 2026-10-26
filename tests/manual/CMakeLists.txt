# Manual (interactive) test programs.
#
# These open a window and need a human to look at the result, so they are
# built but never registered with CTest. Run them directly, e.g.
#   ./build/tests/manual/manual_menubar

function(add_spectra_manual_test name)
    add_executable(manual_${name} ${name}_manual.cpp)
    target_link_libraries(manual_${name} PRIVATE spectra)
    target_compile_features(manual_${name} PRIVATE cxx_std_20)
endfunction()

add_spectra_manual_test(menubar)
add_spectra_manual_test(legend_panel)
add_spectra_manual_test(window_resize)
