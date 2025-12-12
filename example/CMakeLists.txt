cmake_minimum_required(VERSION 3.10)
project(ros_topic_examples)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


if (NOT DEFINED EXTERNAL_LIBS_PATH)
    if (${CMAKE_HOST_SYSTEM_NAME} STREQUAL "Linux")
        set(EXTERNAL_LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../kpp_libs/linux")
    else()
        set(EXTERNAL_LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../kpp_libs/win")
    endif()
endif()

if(EXISTS ${EXTERNAL_LIBS_PATH}/FindCoreDesktop.cmake)
    include(${EXTERNAL_LIBS_PATH}/FindCoreDesktop.cmake)
else()
    message(WARNING "FindCoreDesktop.cmake not found at ${EXTERNAL_LIBS_PATH}. kpp_libs::core may not be available.")
endif()

# Путь к библиотеке arch
set(ARCH_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ros/arch/include")
set(ARCH_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ros/arch/src")
set(ARCH_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../ros/arch")

# Исходные файлы библиотеки
set(ARCH_SOURCES
    ${ARCH_SOURCE_DIR}/communication/impl/factory.cpp
    ${ARCH_SOURCE_DIR}/communication/impl/executor.cpp
    ${ARCH_SOURCE_DIR}/communication/impl/callback_group.cpp
    ${ARCH_SOURCE_DIR}/concurrency/impl/queue_thread_pool.cpp
    ${ARCH_SOURCE_DIR}/communication/impl/wait_set.cpp
)

add_library(arch STATIC ${ARCH_SOURCES})

target_include_directories(arch PUBLIC
    ${ARCH_INCLUDE_DIR}
    ${ARCH_BASE_DIR}
)

target_link_libraries(arch PUBLIC kpp_libs::core pthread)

add_executable(example_01_basic 01_basic_pub_sub.cpp)
target_include_directories(example_01_basic PRIVATE
    ${ARCH_INCLUDE_DIR}
    ${ARCH_BASE_DIR}
)
target_link_libraries(example_01_basic arch pthread kpp_libs::core)

add_executable(example_02_multiple 02_multiple_subscribers.cpp)
target_include_directories(example_02_multiple PRIVATE
    ${ARCH_INCLUDE_DIR}
    ${ARCH_BASE_DIR}
)
target_link_libraries(example_02_multiple arch pthread kpp_libs::core)

add_executable(example_03_qos 03_qos_example.cpp)
target_include_directories(example_03_qos PRIVATE
    ${ARCH_INCLUDE_DIR}
    ${ARCH_BASE_DIR}
)
target_link_libraries(example_03_qos arch pthread kpp_libs::core)

add_executable(example_04_single_consumer 04_single_consumer.cpp)
target_include_directories(example_04_single_consumer PRIVATE
    ${ARCH_INCLUDE_DIR}
    ${ARCH_BASE_DIR}
)
target_link_libraries(example_04_single_consumer arch pthread kpp_libs::core)


