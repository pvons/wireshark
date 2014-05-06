#
# $Id: UseCheckAPI.cmake 53407 2013-11-18 07:42:23Z jmayer $
#
macro( CHECKAPI )
	set( _sources ${ARGN} )

	ADD_CUSTOM_TARGET(checkapi
		COMMAND ${PERL_EXECUTABLE}
		  ${CMAKE_SOURCE_DIR}/tools/checkAPIs.pl
		  -build
		  ${_sources}
		WORKING_DIRECTORY
		  ${CMAKE_SOURCE_DIR}
		DEPENDS
		  ${CMAKE_SOURCE_DIR}/tools/checkAPIs.pl
		  ${_sources}
	)
ENDMACRO()
