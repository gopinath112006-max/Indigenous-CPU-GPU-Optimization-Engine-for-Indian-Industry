cmake_minimum_required(VERSION 3.20)

add_library(hypernova_execution
    thread_pool.cpp
    cuda_driver.cpp
    cuda_kernels_ptx.cpp
    gpu_backend.cpp
)

target_include_directories(hypernova_execution PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include/hypernova/execution>
)

target_link_libraries(hypernova_execution PUBLIC
    hypernova_numerical
    Threads::Threads
)

if(HYPERNOVA_ENABLE_CUDA)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/gpu_cuda.cpp)
        target_sources(hypernova_execution PRIVATE gpu_cuda.cpp)
    endif()
    target_link_libraries(hypernova_execution PUBLIC CUDA::cudart)
endif()

if(HYPERNOVA_ENABLE_HIP)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/gpu_hip.cpp)
        target_sources(hypernova_execution PRIVATE gpu_hip.cpp)
    endif()
    target_link_libraries(hypernova_execution PUBLIC HIP::hip)
endif()

if(HYPERNOVA_ENABLE_SYCL)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/gpu_sycl.cpp)
        target_sources(hypernova_execution PRIVATE gpu_sycl.cpp)
    endif()
    target_link_libraries(hypernova_execution PUBLIC SYCL::sycl)
endif()

install(TARGETS hypernova_execution
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)
install(FILES thread_pool.hpp gpu_backend.hpp DESTINATION include/hypernova/execution)