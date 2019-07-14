AUX_SOURCE_DIRECTORY(. ALL_SRC)
INCLUDE_DIRECTORIES(.)

# remove the lua.c/luac.c from teh all file lists
SET(LIB_SRC ${ALL_SRC})
LIST(REMOVE_ITEM LIB_SRC ./lua.c ./luac.c)
#message("lua src")
#message(${LIB_SRC})

ADD_LIBRARY(lua ${LIB_SRC})
IF (APPLE)
	#set(PLAT "macosx")
	set_target_properties(lua PROPERTIES COMPILE_FLAGS "-Wextra -DLUA_USE_MACOSX")
ELSE()
	#set(PLAT "linux")
	set_target_properties(lua PROPERTIES COMPILE_FLAGS "-Wextra -DLUA_USE_LINUX")
ENDIF(APPLE)

ADD_EXECUTABLE(luaexec lua.c)
target_link_libraries(luaexec lua m dl)
set_target_properties(luaexec PROPERTIES OUTPUT_NAME lua)

#add_executable(luac luac.c)
#target_link_libraries(luac lua)