  cmake_minimum_required(VERSION 3.20)
  project(qr_barcode_scanner LANGUAGES CXX)

  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)

  # İstersen vcpkg OpenCV'yi zorlamak için dışarıdan -DOpenCV_DIR=... geçeceğiz
  find_package(OpenCV REQUIRED)

  # ZBar: önce CONFIG dene, olmazsa include+lib tara
  find_package(ZBar CONFIG QUIET)
  if (ZBar_FOUND)
    message(STATUS "ZBar found via CONFIG")
    set(HAVE_ZBAR_CONFIG 1)
  else()
    message(STATUS "ZBar CONFIG not found; searching headers/libs")
    find_path(ZBAR_INCLUDE_DIR NAMES zbar.h)
    find_library(ZBAR_LIBRARY NAMES zbar)
    if (NOT ZBAR_INCLUDE_DIR OR NOT ZBAR_LIBRARY)
      message(FATAL_ERROR "ZBar not found. Set ZBAR_INCLUDE_DIR / ZBAR_LIBRARY or install mchehab-zbar:x64-windows")
    endif()
  endif()

  add_executable(qr_barcode_scanner main.cpp)
  target_compile_definitions(qr_barcode_scanner PRIVATE LOG_DIR="${CMAKE_SOURCE_DIR}")

  if (HAVE_ZBAR_CONFIG)
    target_link_libraries(qr_barcode_scanner PRIVATE ${OpenCV_LIBS} ZBar::ZBar)
  else()
    target_include_directories(qr_barcode_scanner PRIVATE ${ZBAR_INCLUDE_DIR})
    target_link_libraries(qr_barcode_scanner PRIVATE ${OpenCV_LIBS} ${ZBAR_LIBRARY})
  endif()
