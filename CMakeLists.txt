cmake_minimum_required(VERSION 3.14)
project(KithModule LANGUAGES CXX)

# Include the Logos Module CMake helper (provided by logos-module-builder)
if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LogosModule.cmake")
    include(cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT or ensure logos-module-builder is in your flake inputs.")
endif()

find_package(OpenSSL REQUIRED)

logos_module(
    NAME kith
    SOURCES
        src/kith_impl.h
        src/kith_impl.cpp
        src/contact_store.cpp
        src/contact_sync.h
        src/contact_sync.cpp
        src/logos_transport.hpp
        src/qrcodegen.cpp
    INCLUDE_DIRS src
    LINK_LIBRARIES OpenSSL::Crypto
)
