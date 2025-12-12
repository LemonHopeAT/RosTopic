cmake_minimum_required(VERSION 3.14)

project(ROS_TOPIC LANGUAGES CXX)

set(USE_ASAN ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


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

    ros/arch/src/concurrency/impl/queue_thread_pool.cpp
    ros/arch/src/communication/impl/executor.cpp
    ros/arch/src/communication/impl/callback_group.cpp
    ros/arch/src/communication/impl/wait_set.cpp
    ros/arch/src/communication/impl/factory.cpp
    ros/arch/include/concurrency/impl/queue_thread_pool.h
    ros/arch/include/concurrency/impl/lock_free_MPMC_queue.h
    ros/arch/include/concurrency/impl/lock_free_MPMC_queue_impl.h
    ros/arch/include/experimental/test_message.h
    ros/arch/include/communication/waitable.h
    ros/arch/include/communication/impl/callback_group.h
    ros/arch/include/communication/impl/executor.h
    ros/arch/include/communication/impl/factory.h
    ros/arch/include/communication/impl/publisher.h
    ros/arch/include/communication/impl/qos.h
    ros/arch/include/communication/impl/subscriber_slot.h
    ros/arch/include/communication/impl/subscription.h
    ros/arch/include/communication/impl/topic.h
    ros/arch/include/communication/impl/wait_set.h
    ros/arch/include/communication/impl/topic_registry.h
    ros/arch/include/experimental/benchmark.h
  main.cpp
)

# Add include directories
# ${PROJECT_SOURCE_DIR}/ros/arch/include allows: #include <communication/impl/...>
# ${PROJECT_SOURCE_DIR}/ros/arch allows: #include "include/communication/impl/..."
target_include_directories(ROS_TOPIC PRIVATE 
    ${PROJECT_SOURCE_DIR}/ros/arch/include
    ${PROJECT_SOURCE_DIR}/ros/arch
)

target_link_libraries(ROS_TOPIC  pthread
    kpp_libs::core)

include(GNUInstallDirs)
install(TARGETS ROS_TOPIC
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

add_subdirectory(example/)
