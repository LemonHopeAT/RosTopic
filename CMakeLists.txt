cmake_minimum_required(VERSION 3.14)

project(ROS_TOPIC LANGUAGES CXX)

set(USE_ASAN ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core)

if (NOT DEFINED EXTERNAL_LIBS_PATH)
    if (${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Linux")
        set(EXTERNAL_LIBS_PATH "${PROJECT_SOURCE_DIR}/../kpp_libs/linux")
    else()
        set(EXTERNAL_LIBS_PATH "${PROJECT_SOURCE_DIR}/../kpp_libs/win")
    endif()
endif()

if(USE_ASAN)
  set(CMAKE_CXX_FLAGS
      "${CMAKE_CXX_FLAGS} -fsanitize=address -fsanitize-recover=address")
endif()

include(${EXTERNAL_LIBS_PATH}/FindCoreDesktop.cmake)

add_executable(ROS_TOPIC

    include/CallbackGroup.h
    include/Executor.h
    include/IMessage.h
    include/Publisher.h
    include/Qos.h
    include/SubscriberSlot.h
    include/Topic.h
    include/WaitSet.h


    include/Benchmark.h
  main.cpp
)



target_link_libraries(ROS_TOPIC  pthread Qt${QT_VERSION_MAJOR}::Core
    kpp_libs::core)

include(GNUInstallDirs)
install(TARGETS ROS_TOPIC
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
