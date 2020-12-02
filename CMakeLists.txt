cmake_minimum_required(VERSION 3.17)
project(RemoteBackupClient)

set(CMAKE_CXX_STANDARD 20)

add_executable(RemoteBackupClient main.cpp Message.cpp Message.h)

# see https://cmake.org/cmake/help/latest/module/FindBoost.html
find_package(Boost 1.6.0 REQUIRED COMPONENTS filesystem)
find_package(Openssl)

include_directories(${Boost_INCLUDE_DIR})
# Note: a target should be already defined using 'add_executable' or 'add_library'
target_link_libraries(RemoteBackupClient ${Boost_LIBRARIES} crypto)
