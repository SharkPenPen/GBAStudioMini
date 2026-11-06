# cmake/toolchain-psp.cmake
#
# PSP toolchain（PSPSDK / pspdev）
#
# 使用：
#   $env:PSPDEV = "C:/pspdev"
#   cmake -S . -B build/psp -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-psp.cmake
#
# 注意：本文件只负责“让 CMake 找到 psp-gcc”，
# EBOOT.PBP 的打包在 cmake/psp-package.cmake。
set(PSP 1 CACHE BOOL "" FORCE)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR mips)

if(NOT DEFINED ENV{PSPDEV})
  message(FATAL_ERROR "PSPDEV is not set. Set it to your pspdev root, e.g. C:/pspdev")
endif()

set(PSPDEV "$ENV{PSPDEV}" CACHE PATH "pspdev root")

set(CMAKE_C_COMPILER   "${PSPDEV}/bin/psp-gcc")
set(CMAKE_CXX_COMPILER "${PSPDEV}/bin/psp-g++")
set(CMAKE_AR           "${PSPDEV}/bin/psp-ar")
set(CMAKE_RANLIB       "${PSPDEV}/bin/psp-ranlib")
set(CMAKE_STRIP        "${PSPDEV}/bin/psp-strip")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(PSP_INCLUDE_FLAGS "-I${PSPDEV}/psp/include -I${PSPDEV}/psp/sdk/include")
set(PSP_LIBRARY_FLAGS "-L${PSPDEV}/psp/lib -L${PSPDEV}/psp/sdk/lib")
set(CMAKE_C_FLAGS_INIT   "-G0 -O2 -D_PSP_FW_VERSION=660 ${PSP_INCLUDE_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "-G0 -O2 -D_PSP_FW_VERSION=660 ${PSP_INCLUDE_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,-z,max-page-size=0x1000 -Wl,--gc-sections ${PSP_LIBRARY_FLAGS}")

set(CMAKE_FIND_ROOT_PATH "${PSPDEV}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
