set(RXTECH_THIRD_PARTY_CACHE "${RXTECH_THIRD_PARTY_CACHE}" CACHE PATH "Shared third-party cache root used by server build helpers")
mark_as_advanced(RXTECH_THIRD_PARTY_CACHE)

# ── nlohmann/json (header-only, MIT) ─────────────────────────────────────────
# Resolution order:
# 1. Shared cache at ${RXTECH_THIRD_PARTY_CACHE}/nlohmann/json.hpp
# 2. Vendored copy at ${CMAKE_SOURCE_DIR}/third_party/nlohmann/json.hpp
# 3. CMake FetchContent fallback (requires internet on the build host)
#
find_file(_NLOHMANN_JSON_CACHE_HEADER
    NAMES json.hpp
    PATHS "${RXTECH_THIRD_PARTY_CACHE}/nlohmann"
    NO_DEFAULT_PATH
)
find_file(_NLOHMANN_JSON_LOCAL_HEADER
    NAMES json.hpp
    PATHS "${CMAKE_SOURCE_DIR}/third_party/nlohmann"
    NO_DEFAULT_PATH
)

add_library(nlohmann_json INTERFACE)

if(_NLOHMANN_JSON_CACHE_HEADER)
    get_filename_component(_nlohmann_dir "${_NLOHMANN_JSON_CACHE_HEADER}" DIRECTORY)
    get_filename_component(_nlohmann_dir "${_nlohmann_dir}" DIRECTORY)
    target_include_directories(nlohmann_json INTERFACE "${_nlohmann_dir}")
    message(STATUS "nlohmann/json: using shared cache at ${_NLOHMANN_JSON_CACHE_HEADER}")
elseif(_NLOHMANN_JSON_LOCAL_HEADER)
    target_include_directories(nlohmann_json INTERFACE
        "${CMAKE_SOURCE_DIR}/third_party")
    message(STATUS "nlohmann/json: using vendored copy at ${_NLOHMANN_JSON_LOCAL_HEADER}")
else()
    include(FetchContent)
    FetchContent_Declare(nlohmann_json_fetch
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
        DOWNLOAD_NO_EXTRACT TRUE
        DOWNLOAD_DIR "${CMAKE_BINARY_DIR}/_deps/nlohmann"
    )
    FetchContent_MakeAvailable(nlohmann_json_fetch)
    target_include_directories(nlohmann_json INTERFACE
        "${CMAKE_BINARY_DIR}/_deps/nlohmann/..")
    message(STATUS "nlohmann/json: downloaded via FetchContent")
endif()

# ── spdlog (optional structured logging backend) ───────────────────────────
add_library(rxtech_spdlog INTERFACE)

if(RXTECH_ENABLE_SPDLOG)
    set(_RXTECH_HAS_SPDLOG FALSE)

    find_package(spdlog QUIET)

    if(TARGET spdlog::spdlog_header_only)
        target_link_libraries(rxtech_spdlog INTERFACE spdlog::spdlog_header_only)
        target_compile_definitions(rxtech_spdlog INTERFACE RXTECH_HAS_SPDLOG=1)
        set(_RXTECH_HAS_SPDLOG TRUE)
        message(STATUS "spdlog: using installed package target spdlog::spdlog_header_only")
    elseif(TARGET spdlog::spdlog)
        target_link_libraries(rxtech_spdlog INTERFACE spdlog::spdlog)
        target_compile_definitions(rxtech_spdlog INTERFACE RXTECH_HAS_SPDLOG=1)
        set(_RXTECH_HAS_SPDLOG TRUE)
        message(STATUS "spdlog: using installed package target spdlog::spdlog")
    else()
        find_path(_SPDLOG_CACHE_INCLUDE_DIR
            NAMES spdlog/spdlog.h
            PATHS "${RXTECH_THIRD_PARTY_CACHE}/spdlog/include"
            NO_DEFAULT_PATH
        )
        find_path(_SPDLOG_LOCAL_INCLUDE_DIR
            NAMES spdlog/spdlog.h
            PATHS "${CMAKE_SOURCE_DIR}/third_party/spdlog/include"
            NO_DEFAULT_PATH
        )

        if(_SPDLOG_CACHE_INCLUDE_DIR)
            target_include_directories(rxtech_spdlog INTERFACE "${_SPDLOG_CACHE_INCLUDE_DIR}")
            target_compile_definitions(rxtech_spdlog INTERFACE RXTECH_HAS_SPDLOG=1)
            set(_RXTECH_HAS_SPDLOG TRUE)
            message(STATUS "spdlog: using shared cache include dir ${_SPDLOG_CACHE_INCLUDE_DIR}")
        elseif(_SPDLOG_LOCAL_INCLUDE_DIR)
            target_include_directories(rxtech_spdlog INTERFACE "${_SPDLOG_LOCAL_INCLUDE_DIR}")
            target_compile_definitions(rxtech_spdlog INTERFACE RXTECH_HAS_SPDLOG=1)
            set(_RXTECH_HAS_SPDLOG TRUE)
            message(STATUS "spdlog: using vendored include dir ${_SPDLOG_LOCAL_INCLUDE_DIR}")
        else()
            message(STATUS "spdlog: dependency not found, falling back to built-in structured logger backend")
        endif()
    endif()

    if(NOT _RXTECH_HAS_SPDLOG)
        target_compile_definitions(rxtech_spdlog INTERFACE RXTECH_HAS_SPDLOG=0)
    endif()
else()
    target_compile_definitions(rxtech_spdlog INTERFACE RXTECH_HAS_SPDLOG=0)
endif()
