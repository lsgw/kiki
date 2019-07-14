set(SOURCES
    Document.cpp
    Schema.cpp
    Userdata.hpp
    file.hpp
    luax.hpp
    rapidjson.cpp
    values.cpp
    values.hpp
)
add_library(rapidjson SHARED ${SOURCES})
set_target_properties(rapidjson PROPERTIES SUFFIX ".so")