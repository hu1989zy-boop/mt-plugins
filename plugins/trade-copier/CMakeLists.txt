cmake_minimum_required(VERSION 3.21)
project(trade_copier LANGUAGES CXX)

# Keep tests discoverable when building this plugin or the whole repository.
include(CTest)

option(TRADECOPIER_BUILD_MT5 "Build the MT5 trade-copier implementation" ON)
if(TRADECOPIER_BUILD_MT5)
  add_subdirectory(mt5)
endif()

option(TRADECOPIER_BUILD_MT4 "Build the MT4 trade-copier implementation" ON)
if(TRADECOPIER_BUILD_MT4)
  add_subdirectory(mt4)
endif()
