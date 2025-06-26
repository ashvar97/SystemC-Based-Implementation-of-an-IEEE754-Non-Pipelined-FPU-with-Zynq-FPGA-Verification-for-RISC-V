#include <sc_tool/SCTool.h>  

const char* __sctool_args_str = R"( "/root/sc_tools/designs/my_project/build/mydesign.sctool.cpp" "-sv_out" "/root/sc_tools/designs/my_project/build/sv_out/mydesign.sv" "-init_local_vars" "--" "-D__SC_TOOL__" "-D__SC_TOOL_ANALYZE__" "-DNDEBUG" "-DSC_ALLOW_DEPRECATED_IEEE_API" "-Wno-logical-op-parentheses" "-std=c++17" "-I/root/sc_tools/include" "-I/root/sc_tools/include" "-I/root/sc_tools/include/sctcommon" "-I/root/sc_tools/include/sctmemory" "-I/root/sc_tools/include/sctmemory/utils" "-I/usr/include/c++/9" "-I/usr/include/x86_64-linux-gnu/c++/9" "-I/usr/include/c++/9/backward" "-I/usr/lib/gcc/x86_64-linux-gnu/9/include" "-I/usr/local/include" "-I/usr/include/x86_64-linux-gnu" "-I/usr/include" "-I/root/sc_tools/lib/clang/18.1.8/include" "-I/root/sc_tools/designs/my_project/sc_elab2/lib")"; 

#include "/root/sc_tools/designs/my_project/example.cpp"
