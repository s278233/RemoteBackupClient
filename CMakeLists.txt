cmake_minimum_required(VERSION 3.16)
project(RemoteBackupClient)

set(CMAKE_CXX_STANDARD 20)

add_executable(RemoteBackupClient main.cpp Message.cpp Message.h FileWatcher.cpp FileWatcher.h SafeCout.h)

if(CMAKE_SYSTEM_NAME STREQUAL Linux OR CMAKE_SYSTEM_NAME STREQUAL Darwin)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    find_package(Threads REQUIRED)
endif ()

# see https://cmake.org/cmake/help/latest/module/FindBoost.html
find_package(Boost 1.6.0 REQUIRED COMPONENTS filesystem serialization)

include_directories(${Boost_INCLUDE_DIR})
# Note: a target should be already defined using 'add_executable' or 'add_library'
if(CMAKE_SYSTEM_NAME STREQUAL Linux OR CMAKE_SYSTEM_NAME STREQUAL Darwin)
    target_link_libraries(RemoteBackupClient PRIVATE Threads::Threads ${Boost_LIBRARIES} crypto ssl)
else()
    target_link_libraries(RemoteBackupClient ${Boost_LIBRARIES} crypto ssl)
endif()

#If "file does not belong to any project" warning appears just do:
#File -> Invalidate Caches/Restart -> Invalidate And Restart.