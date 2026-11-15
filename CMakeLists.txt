cmake_minimum_required(VERSION 3.21)
project(GTASACoop VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Alvos por plataforma
#   Windows x86  -> dinput8.dll        (DLL proxy carregada pelo gta_sa.exe 1.0 US)
#   Android      -> libGTASA_Coop.so   (armeabi-v7a / arm64-v8a, carregada pelo APK)
#   Qualquer host-> coop_core + testes (nucleo portavel, sem o jogo)
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

option(COOP_BUILD_TESTS "Compila os testes do nucleo portavel" ON)
option(COOP_WERROR      "Trata warnings como erros (CI)"       OFF)

if(ANDROID)
  set(COOP_PLATFORM android)
elseif(WIN32)
  set(COOP_PLATFORM win32)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR
      "gta_sa.exe e um binario de 32 bits: a DLL precisa ser x86.\n"
      "  MSVC : cmake -A Win32 ...\n"
      "  MinGW: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-i686.cmake ...")
  endif()
else()
  set(COOP_PLATFORM host)
endif()
message(STATUS "GTASACoop: plataforma = ${COOP_PLATFORM}")

# ---------------------------------------------------------------------------
# Ponto flutuante: sem fast-math e sem contracao FMA, para que o nucleo gere
# os mesmos resultados em x86 e ARM (ver docs/ARCHITECTURE.md secao 6).
# ---------------------------------------------------------------------------
add_library(coop_options INTERFACE)
if(MSVC)
  target_compile_options(coop_options INTERFACE /W4 /fp:precise /utf-8 /permissive-
    $<$<BOOL:${COOP_WERROR}>:/WX>)
  target_compile_definitions(coop_options INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
  # x86: SSE2 em vez de x87 => float de 32 bits reais, igual ao NEON/VFP do ARM.
  target_compile_options(coop_options INTERFACE /arch:SSE2)
else()
  target_compile_options(coop_options INTERFACE -Wall -Wextra -fno-fast-math -ffp-contract=off
    $<$<BOOL:${COOP_WERROR}>:-Werror>)
  if(WIN32)
    target_compile_options(coop_options INTERFACE -msse2 -mfpmath=sse)
    target_compile_definitions(coop_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN)
  endif()
endif()

# ---------------------------------------------------------------------------
# Dependencias (versoes fixadas por tag; nada e baixado em runtime no usuario)
# ---------------------------------------------------------------------------
include(FetchContent)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5) # CMakeLists antigos do ENet/MinHook em CMake >= 4
# CRT estatica (/MT) em TODOS os alvos, inclusive dependencias: a DLL nao pode
# exigir o Visual C++ Redistributable na maquina do jogador.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

FetchContent_Declare(enet
  GIT_REPOSITORY https://github.com/lsalzman/enet.git
  GIT_TAG        v1.3.18
  GIT_SHALLOW    TRUE
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(enet)
target_include_directories(enet SYSTEM PUBLIC ${enet_SOURCE_DIR}/include)
if(WIN32)
  target_link_libraries(enet ws2_32 winmm) # assinatura simples: a mesma usada pelo CMakeLists do ENet
endif()

if(COOP_PLATFORM STREQUAL "win32")
  FetchContent_Declare(minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        v1.3.4
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(minhook)
endif()

# ---------------------------------------------------------------------------
# Nucleo portavel: rede, netcode, politica de script. Nao inclui nenhum header
# do jogo nem do SO -> compila e e testado em qualquer host.
# ---------------------------------------------------------------------------
add_library(coop_core STATIC
  src/core/Runtime.cpp
  src/core/net/Transport.cpp
  src/core/sync/SnapshotBuffer.cpp
  src/core/script/OpcodePolicy.cpp)
target_include_directories(coop_core PUBLIC include src/core)
target_link_libraries(coop_core PUBLIC enet PRIVATE coop_options)

# ---------------------------------------------------------------------------
# Windows: dinput8.dll (proxy)
# ---------------------------------------------------------------------------
if(COOP_PLATFORM STREQUAL "win32")
  add_library(coop_win32 SHARED
    platform/win32/dllmain.cpp
    platform/win32/Menu.cpp
    platform/win32/SystemFixes.cpp
    platform/win32/dinput8.def)
  set_target_properties(coop_win32 PROPERTIES OUTPUT_NAME dinput8 PREFIX "")
  target_include_directories(coop_win32 PRIVATE platform)
  target_link_libraries(coop_win32 PRIVATE coop_core coop_options minhook)
  if(MINGW)
    # DLL sem dependencia de libgcc/libstdc++/winpthread ao lado do jogo.
    target_link_options(coop_win32 PRIVATE -static -Wl,--enable-stdcall-fixup)
  endif()
  install(TARGETS coop_win32 RUNTIME DESTINATION .)
endif()

# ---------------------------------------------------------------------------
# Android: libGTASA_Coop.so
# ---------------------------------------------------------------------------
if(COOP_PLATFORM STREQUAL "android")
  add_library(GTASA_Coop SHARED
    platform/android/entry.cpp
    platform/android/GotHook.cpp)
  target_include_directories(GTASA_Coop PRIVATE platform)
  target_link_libraries(GTASA_Coop PRIVATE coop_core coop_options log)
  target_link_options(GTASA_Coop PRIVATE -Wl,--exclude-libs,ALL -Wl,-z,max-page-size=16384)
  install(TARGETS GTASA_Coop LIBRARY DESTINATION lib/${ANDROID_ABI})
endif()

# ---------------------------------------------------------------------------
# Testes do nucleo (rodam no CI em Linux; nao precisam do jogo)
# ---------------------------------------------------------------------------
if(COOP_BUILD_TESTS AND COOP_PLATFORM STREQUAL "host")
  enable_testing()
  add_executable(coop_tests tests/test_core.cpp)
  target_link_libraries(coop_tests PRIVATE coop_core coop_options)
  add_test(NAME coop_core COMMAND coop_tests)
endif()
