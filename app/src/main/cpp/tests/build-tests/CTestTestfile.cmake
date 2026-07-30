# CMake generated Testfile for 
# Source directory: /data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests
# Build directory: /data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/build-tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
include("/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/build-tests/gammatone_numerical_stability[1]_include.cmake")
include("/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/build-tests/no_denormals_low_level[1]_include.cmake")
include("/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/build-tests/dsp_core_stability[1]_include.cmake")
include("/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/build-tests/test_regression_tuning[1]_include.cmake")
add_test([=[test_adaptive_engine]=] "/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/build-tests/test_adaptive_engine")
set_tests_properties([=[test_adaptive_engine]=] PROPERTIES  FAIL_REGULAR_EXPRESSION "FALLARON" _BACKTRACE_TRIPLES "/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/CMakeLists.txt;92;add_test;/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/CMakeLists.txt;0;")
add_test([=[test_close_loop]=] "/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/build-tests/test_close_loop")
set_tests_properties([=[test_close_loop]=] PROPERTIES  _BACKTRACE_TRIPLES "/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/CMakeLists.txt;113;add_test;/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/CMakeLists.txt;0;")
add_test([=[test_stability]=] "/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/build-tests/test_stability")
set_tests_properties([=[test_stability]=] PROPERTIES  FAIL_REGULAR_EXPRESSION "FALLARON" _BACKTRACE_TRIPLES "/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/CMakeLists.txt;137;add_test;/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/CMakeLists.txt;0;")
add_test([=[test_control_frame_bus_stress]=] "/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/build-tests/test_control_frame_bus_stress")
set_tests_properties([=[test_control_frame_bus_stress]=] PROPERTIES  FAIL_REGULAR_EXPRESSION "FALLARON" _BACKTRACE_TRIPLES "/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/CMakeLists.txt;170;add_test;/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/CMakeLists.txt;0;")
add_test([=[test_audio_bus]=] "/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/build-tests/test_audio_bus")
set_tests_properties([=[test_audio_bus]=] PROPERTIES  FAIL_REGULAR_EXPRESSION "FALLARON" _BACKTRACE_TRIPLES "/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/CMakeLists.txt;196;add_test;/data/data/com.termux/files/home/IVANNA-OMEGA-SUPREME/app/src/main/cpp/tests/CMakeLists.txt;0;")
subdirs("_deps/googletest-build")
