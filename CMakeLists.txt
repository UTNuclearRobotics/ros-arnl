# http://ros.org/doc/groovy/api/catkin/html/user_guide/supposed.html
cmake_minimum_required(VERSION 2.8.3)
project(rosarnl)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -pg")


# Load catkin and all dependencies required for this package
# TODO: remove all from COMPONENTS that are not catkin packages.
find_package(catkin REQUIRED COMPONENTS message_generation roscpp nav_msgs geometry_msgs tf actionlib actionlib_msgs)

# Set the build type.  Options are:
#  Coverage       : w/ debug symbols, w/o optimization, w/ code-coverage
#  Debug          : w/ debug symbols, w/o optimization
#  Release        : w/o debug symbols, w/ optimization
#  RelWithDebInfo : w/ debug symbols, w/ optimization
#  MinSizeRel     : w/o debug symbols, w/ optimization, stripped binaries
set(ROS_BUILD_TYPE RelWithDebInfo)

#set the default path for built executables to the "bin" directory
#set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
#set the default path for built libraries to the "lib" directory
#set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

#uncomment if you have defined messages
add_message_files(
  FILES
  BatteryStatus.msg
)

#uncomment if you have defined services
add_service_files(
  FILES
  WheelLight.srv
)

## Generate added messages and services with any dependencies listed here
generate_messages(
  DEPENDENCIES
  std_msgs
)

catkin_package(
  INCLUDE_DIRS
  CATKIN_DEPENDS message_generation roscpp nav_msgs geometry_msgs tf actionlib actionlib_msgs
)

find_package(Boost REQUIRED COMPONENTS thread)
include_directories(${catkin_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS} )

IF(DEFINED ENV{ARNL})
  message(WARNING "Warning: Using ARNL environment variable as location of ARNL include and lib directories: $ENV{ARNL}")
  if(EXISTS $ENV{ARNL}/include/Arnl.h)
    include_directories( $ENV{ARNL}/include $ENV{ARNL}/include/Aria $ENV{ARNL}/include/ArNetworking)
    link_directories($ENV{ARNL}/lib)
  else()
    message(FATAL_ERROR "$ENV{ARNL}/include/Arnl.h file not found. Check value of ARNL environment variable or unset to use default installation directory.")
  endif()
ELSE()
  if(EXISTS /usr/local/Arnl/include/Arnl.h)
    include_directories( /usr/local/Arnl/include /usr/local/Arnl/include/Aria /usr/local/Arnl/include/ArNetworking)
    link_directories(/usr/local/Arnl/lib)
  else()
    message(FATAL_ERROR "/usr/local/Arnl/include/Arnl.h file not found. ros-arnl requires the Adept MobileRobots ARNL and BaseArnl libraries to be installed. Set ARNL environment variable to use from non standard intallation directory.")
  endif()
ENDIF()

include_directories( /usr/local/Aria/ArSpeechSynth_Cepstral/include)
link_directories(/usr/local/Aria/lib)

add_executable(rosarnl_node rosarnl_node.cpp ArnlSystem.cpp RobotMonitor.cpp LaserPublisher.cpp)
add_dependencies(rosarnl_node ${catkin_EXPORTED_TARGETS} ${PROJECT_NAME}_generate_messages_cpp})
#add_dependencies(rosarnl_node rosarnl_gencpp)

target_link_libraries(rosarnl_node ${catkin_LIBRARIES} ${Boost_LIBRARIES} Arnl BaseArnl ArNetworkingForArnl ArSpeechSynth_Cepstral swift AriaForArnl pthread dl rt)
set_target_properties(rosarnl_node PROPERTIES COMPILE_FLAGS "-fPIC -D_REENTRANT -Wall")

#############
## Install ##
#############

## Mark executables and/or libraries for installation
install(
  TARGETS 
    rosarnl_node
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)
