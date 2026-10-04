# AlienModUI.dll + AlienInjectorUI.exe + AlienModSetup.exe (x86). Исходники мода лежат в корне папки. Сборка — devuild.bat.
#   Visual Studio:  cmake -S . -B build -A Win32 && cmake --build build --config Release
#   MSYS2 MINGW32:  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
cmake_minimum_required(VERSION 3.20)
project(AlienModStandalone LANGUAGES C CXX RC)
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "AI.exe 32-битный: собирай x86 (Visual Studio: -A Win32; MSYS2: mingw32)")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${cfg} ${CMAKE_CURRENT_SOURCE_DIR})  # exe и dll — рядом, в корне
endforeach()

file(GLOB MOD_SRC ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
set(IMGUI libs/imgui)
add_library(AlienMod SHARED ${MOD_SRC} standalone/standalone.cpp
  ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp ${IMGUI}/imgui_demo.cpp
  ${IMGUI}/backends/imgui_impl_dx11.cpp ${IMGUI}/backends/imgui_impl_win32.cpp
  libs/minhook/src/buffer.c libs/minhook/src/hook.c libs/minhook/src/trampoline.c libs/minhook/src/hde/hde32.c)
set_target_properties(AlienMod PROPERTIES PREFIX "" OUTPUT_NAME AlienModUI)
target_include_directories(AlienMod PRIVATE . ${IMGUI} ${IMGUI}/backends libs/minhook/include)
target_link_libraries(AlienMod PRIVATE winmm user32 gdi32 imm32 dwmapi advapi32)  # d3dcompiler — LoadLibrary (standalone.cpp)

add_executable(AlienInjector inject/main.cpp)
target_link_libraries(AlienInjector PRIVATE shell32 user32)
set_target_properties(AlienInjector PROPERTIES OUTPUT_NAME AlienInjectorUI)

# установщик: находит игру, «Обзор…» — обычный выбор папки Проводника, ставит/убирает BML мода
add_executable(AlienModSetup WIN32 setup/setup.cpp setup/setup.rc)
target_link_libraries(AlienModSetup PRIVATE ole32 shell32 comctl32 advapi32 uuid gdi32 user32)
if(MSVC)
  target_link_options(AlienModSetup PRIVATE /MANIFEST:NO)  # свой манифест — в setup.rc
endif()

foreach(t AlienMod AlienInjector AlienModSetup)
  if(MSVC)
    target_compile_options(${t} PRIVATE /utf-8 /W3 /EHsc /D_CRT_SECURE_NO_WARNINGS)
  else()
    target_compile_options(${t} PRIVATE -O2 -msse2 -mfpmath=sse -fno-strict-aliasing)
    target_link_options(${t} PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
endforeach()
if(NOT MSVC)
  target_link_options(AlienMod PRIVATE -Wl,--kill-at)  # экспорт AlienModUnload без @4 (его зовёт инжектор)
endif()
