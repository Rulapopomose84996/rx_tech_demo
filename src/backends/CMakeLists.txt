if(BUILD_AF_XDP_BACKEND)
    add_subdirectory(af_xdp)
endif()

if(BUILD_DPDK_BACKEND)
    add_subdirectory(dpdk)
endif()
