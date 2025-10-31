cmake_minimum_required(VERSION 3.20)
project(qr_barcode_scanner LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)

add_executable(qr_barcode_scanner main.cpp)
target_link_libraries(qr_barcode_scanner PRIVATE ${OpenCV_LIBS})
