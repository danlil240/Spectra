# CMake generated Testfile for 
# Source directory: /home/daniel/projects/plotix/tests
# Build directory: /home/daniel/projects/plotix/build/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[unit_test_transform]=] "/home/daniel/projects/plotix/build/tests/unit_test_transform")
set_tests_properties([=[unit_test_transform]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/daniel/projects/plotix/tests/CMakeLists.txt;34;add_test;/home/daniel/projects/plotix/tests/CMakeLists.txt;38;add_plotix_test;/home/daniel/projects/plotix/tests/CMakeLists.txt;0;")
add_test([=[unit_test_layout]=] "/home/daniel/projects/plotix/build/tests/unit_test_layout")
set_tests_properties([=[unit_test_layout]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/daniel/projects/plotix/tests/CMakeLists.txt;34;add_test;/home/daniel/projects/plotix/tests/CMakeLists.txt;39;add_plotix_test;/home/daniel/projects/plotix/tests/CMakeLists.txt;0;")
add_test([=[unit_test_timeline]=] "/home/daniel/projects/plotix/build/tests/unit_test_timeline")
set_tests_properties([=[unit_test_timeline]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/daniel/projects/plotix/tests/CMakeLists.txt;34;add_test;/home/daniel/projects/plotix/tests/CMakeLists.txt;40;add_plotix_test;/home/daniel/projects/plotix/tests/CMakeLists.txt;0;")
add_test([=[unit_test_easing]=] "/home/daniel/projects/plotix/build/tests/unit_test_easing")
set_tests_properties([=[unit_test_easing]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/daniel/projects/plotix/tests/CMakeLists.txt;34;add_test;/home/daniel/projects/plotix/tests/CMakeLists.txt;41;add_plotix_test;/home/daniel/projects/plotix/tests/CMakeLists.txt;0;")
add_test([=[unit_test_ring_buffer]=] "/home/daniel/projects/plotix/build/tests/unit_test_ring_buffer")
set_tests_properties([=[unit_test_ring_buffer]=] PROPERTIES  _BACKTRACE_TRIPLES "/home/daniel/projects/plotix/tests/CMakeLists.txt;34;add_test;/home/daniel/projects/plotix/tests/CMakeLists.txt;42;add_plotix_test;/home/daniel/projects/plotix/tests/CMakeLists.txt;0;")
subdirs("../_deps/googletest-build")
