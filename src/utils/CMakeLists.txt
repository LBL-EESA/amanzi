# -*- mode: cmake -*-


#
#  Amanzi Tool Kit (ATK)
#    General utilities
#    This directory has become a dumping ground for things that do not
#    have a well-defined home. Will go away or get renamed. For now we
#    need to keep it around.
#

# Amanzi module, include files found in AMANZI_MODULE_PATH
include(PrintVariable)
include(LibraryManager)

#
# Define a project name
# After this command the following varaibles are defined
#   ATK_SOURCE_DIR
#   ATK_BINARY_DIR
# Other projects (subdirectories) can reference this directory
# through these variables.
project(ATK)

include_directories(${Boost_INCLUDE_DIRS})
include_directories(${Teuchos_INCLUDE_DIRS})
include_directories(${DBC_SOURCE_DIR})


add_amanzi_library(atk
                   SOURCE Timer.cc 
                          TimerManager.cc
                          TimeStepManager.cc
                          Units.cc
                          XMLParameterListWriter.cc 
                          LeastSquare.cc 
                          VerboseObject.cc
                          IOEvent.cc
                          Spline.cc
                   HEADERS HDF5Reader.hh
                           Utils.hh
                           Units.hh
                           Timer.hh
                           TimerManager.hh
                           TimeStepManager.hh
                           XMLParameterListWriter.hh
                           VerboseObject.hh
                           VerboseObject_objs.hh
                           GlobalVerbosity.hh
                           IOEvent.hh
                           Iterators.hh
                           factory.hh
                           factory_with_state.hh
                           LeastSquare.hh
                           ObservationData.hh
                           Spline.hh
                   )

if (BUILD_TESTS)
  
    # Add UnitTest include directoy
    include_directories(${UnitTest_INCLUDE_DIRS})

    # Copy test directory files if an out of source build
    if (NOT (${ATK_SOURCE_DIR} EQUAL ${ATK_BINARY_DIR}))
        execute_process(COMMAND ${CMAKE_COMMAND} -E 
          copy_directory ${ATK_SOURCE_DIR}/test ${ATK_BINARY_DIR}/test) 
    endif()

    # Add the utils directory to the include paths
    include_directories(${ATK_SOURCE_DIR})

    ADD_AMANZI_TEST(utils_time_step_manager utils_time_step_manager
                    KIND unit
                    SOURCE test/Main.cc test/utils_time_step_manager.cc
                    LINK_LIBS atk ${UnitTest_LIBRARIES} ${Teuchos_LIBRARIES})

    ADD_AMANZI_TEST(utils_verbosity utils_verbosity
                    KIND unit
                    SOURCE test/Main.cc test/utils_verboseobject.cc
                    LINK_LIBS atk ${UnitTest_LIBRARIES} ${Teuchos_LIBRARIES})

    ADD_AMANZI_TEST(utils_units utils_units
                    KIND unit
                    SOURCE test/Main.cc test/utils_units.cc
                    LINK_LIBS atk ${UnitTest_LIBRARIES} ${Teuchos_LIBRARIES})

    ADD_AMANZI_TEST(utils_spline utils_spline
                    KIND unit
                    SOURCE test/Main.cc test/utils_spline.cc
                    LINK_LIBS atk error_handling atk ${UnitTest_LIBRARIES} ${Teuchos_LIBRARIES})
                  
endif()









