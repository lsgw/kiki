add_library(http SHARED http.cpp)
target_link_libraries(http httpparser)
set_target_properties(http PROPERTIES LINK_FLAGS "-fPIC")
set_target_properties(http PROPERTIES SUFFIX ".so")