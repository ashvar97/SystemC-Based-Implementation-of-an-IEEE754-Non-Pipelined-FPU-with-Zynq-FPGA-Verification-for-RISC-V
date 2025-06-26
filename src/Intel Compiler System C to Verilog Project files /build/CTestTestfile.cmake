# CMake generated Testfile for 
# Source directory: /root/sc_tools/designs/my_project
# Build directory: /root/sc_tools/designs/my_project/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(FPU_BUILD "/usr/local/bin/cmake" "--build" "/root/sc_tools/designs/my_project/build" "--target" "FPU_sctool")
set_tests_properties(FPU_BUILD PROPERTIES  _BACKTRACE_TRIPLES "/root/sc_tools/lib/cmake/SVC/svc_target.cmake;237;add_test;/root/sc_tools/designs/my_project/CMakeLists.txt;31;svc_target;/root/sc_tools/designs/my_project/CMakeLists.txt;0;")
add_test(FPU_SYN "/root/sc_tools/designs/my_project/build/FPU_sctool")
set_tests_properties(FPU_SYN PROPERTIES  DEPENDS "FPU_BUILD" WILL_FAIL "FALSE" _BACKTRACE_TRIPLES "/root/sc_tools/lib/cmake/SVC/svc_target.cmake;242;add_test;/root/sc_tools/designs/my_project/CMakeLists.txt;31;svc_target;/root/sc_tools/designs/my_project/CMakeLists.txt;0;")
