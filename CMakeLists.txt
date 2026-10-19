cmake_minimum_required(VERSION 3.21)
project(mt_plugins LANGUAGES CXX)

# Enable discovery of plugin tests from the repository build directory.
include(CTest)

option(MT_PLUGINS_BUILD_TRADE_COPIER "Build the trade-copier plugin project" ON)
if(MT_PLUGINS_BUILD_TRADE_COPIER)
  add_subdirectory(plugins/trade-copier)
endif()

option(MT_PLUGINS_BUILD_CLOSED_TRADE_ALLOCATOR "Build closed-trade-allocator" ON)
if(MT_PLUGINS_BUILD_CLOSED_TRADE_ALLOCATOR)
  add_subdirectory(plugins/closed-trade-allocator)
endif()

# account-status-updater is a documentation-only scaffold until implemented.

option(MT_PLUGINS_BUILD_DYNAMIC_LEVERAGE "Build dynamic-leverage" ON)
if(MT_PLUGINS_BUILD_DYNAMIC_LEVERAGE)
  add_subdirectory(plugins/dynamic-leverage)
endif()
