# Examples — each is a standalone executable linking against plotix

function(add_plotix_example name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE plotix)
    target_compile_features(${name} PRIVATE cxx_std_20)
endfunction()

add_plotix_example(basic_line)
add_plotix_example(live_stream)
add_plotix_example(animated_scatter)
add_plotix_example(multi_subplot)
add_plotix_example(multi_figure_demo)
add_plotix_example(offscreen_export)
add_plotix_example(video_record)
add_plotix_example(window_resize_test)
add_plotix_example(logger_example)
add_plotix_example(test_menubar)
