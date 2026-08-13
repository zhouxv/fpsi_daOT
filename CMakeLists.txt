cmake_minimum_required(VERSION 3.22)
project(MyProj)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_STANDARD 20)

if(NOT DEFINED VOLEPSI_INSTALL_PATH)
  set(VOLEPSI_INSTALL_PATH "${CMAKE_SOURCE_DIR}/volepsi")
endif()

if(NOT DEFINED CATCH2_INSTALL_PATH)
  set(CATCH2_INSTALL_PATH "${CMAKE_SOURCE_DIR}/catch2")
endif()

set(CMAKE_PREFIX_PATH "${VOLEPSI_INSTALL_PATH};${CATCH2_INSTALL_PATH}")

find_package(Catch2 3 REQUIRED)
find_package(volePSI REQUIRED)


if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address)
  add_link_options(-fsanitize=address)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  add_compile_options(-O3)
endif()

message(CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

set(SOURCES
  ${CMAKE_SOURCE_DIR}/sparseComp/Common/BaxosUtils.cpp
  ${CMAKE_SOURCE_DIR}/sparseComp/Common/BlockUtils.cpp
  ${CMAKE_SOURCE_DIR}/sparseComp/CustomOPRF/CustomizedOPRF.cpp
  ${CMAKE_SOURCE_DIR}/sparseComp/MultiOPRF/MultiOPRF.cpp
  ${CMAKE_SOURCE_DIR}/sparseComp/Common/HashUtils.cpp
  ${CMAKE_SOURCE_DIR}/sparseComp/Common/Common.cpp
)
set(HEADERS
  ${CMAKE_SOURCE_DIR}/sparseComp/Common/Common.h
  ${CMAKE_SOURCE_DIR}/sparseComp/Common/BaxosUtils.h
  ${CMAKE_SOURCE_DIR}/sparseComp/Common/BlockUtils.h
  ${CMAKE_SOURCE_DIR}/sparseComp/CustomOPRF/CustomizedOPRF.h
  ${CMAKE_SOURCE_DIR}/sparseComp/MultiOPRF/MultiOPRF.h
  ${CMAKE_SOURCE_DIR}/sparseComp/SpBSOT/SpBSOT.h
  ${CMAKE_SOURCE_DIR}/sparseComp/BlockSpBSOT/BlockSpBSOT.h
  ${CMAKE_SOURCE_DIR}/sparseComp/Common/VecMatrix.h
  ${CMAKE_SOURCE_DIR}/sparseComp/Common/ZN.h
  ${CMAKE_SOURCE_DIR}/sparseComp/Common/HashUtils.h
  ${CMAKE_SOURCE_DIR}/sparseComp/SpBZeroCheck/SpBZeroCheck.h
  ${CMAKE_SOURCE_DIR}/sparseComp/SpLInf/SpLInf.h
  ${CMAKE_SOURCE_DIR}/sparseComp/SpL1/SpL1.h
  ${CMAKE_SOURCE_DIR}/sparseComp/SpL2/SpL2.h
  ${CMAKE_SOURCE_DIR}/sparseComp/SpLInf/SpLInfPre.h
  ${CMAKE_SOURCE_DIR}/sparseComp/SpL1/SpL1Pre.h
  ${CMAKE_SOURCE_DIR}/sparseComp/SpL2/SpL2Pre.h
  ${CMAKE_SOURCE_DIR}/sparseComp/Common/SockUtils.h
  ${CMAKE_SOURCE_DIR}/sparseComp/FuzzyLInf/FuzzyLInf.h
  ${CMAKE_SOURCE_DIR}/sparseComp/FuzzyL1/FuzzyL1.h
  ${CMAKE_SOURCE_DIR}/sparseComp/FuzzyL2/FuzzyL2.h
  ${CMAKE_SOURCE_DIR}/sparseComp/FuzzyLInf/FuzzyLInfPre.h
  ${CMAKE_SOURCE_DIR}/sparseComp/FuzzyL1/FuzzyL1Pre.h
  ${CMAKE_SOURCE_DIR}/sparseComp/FuzzyL2/FuzzyL2Pre.h
)

if(BUILD_BENCH)
  set(ALL_BENCHS
    fuzzylinf_bench
    fuzzyl1_bench
    fuzzyl2_bench
  )

  set(TEST_SOURCE_PREFIX ${CMAKE_SOURCE_DIR}/tests)

  add_executable(fuzzylinf_bench ${TEST_SOURCE_PREFIX}/FuzzyLInf.bench.cpp ${SOURCES})
  add_executable(fuzzyl1_bench ${TEST_SOURCE_PREFIX}/FuzzyL1.bench.cpp ${SOURCES})
  add_executable(fuzzyl2_bench ${TEST_SOURCE_PREFIX}/FuzzyL2.bench.cpp ${SOURCES})


  foreach(target ${ALL_BENCHS})
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_options(${target} PRIVATE ${DEFAULT_COMPILER_OPTIONS_AND_WARNINGS})
    target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain visa::volePSI)

    add_test(NAME ${target} COMMAND ${target})
  endforeach()
endif()

if(BUILD_MAIN)
  add_executable(main ${CMAKE_SOURCE_DIR}/frontend/main.cpp ${SOURCES})
  target_link_libraries(${target} PRIVATE visa::volePSI)
  target_compile_options(${target} PRIVATE ${DEFAULT_COMPILER_OPTIONS_AND_WARNINGS})
endif()

