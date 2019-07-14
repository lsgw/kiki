add_library(serialize SHARED serialize.c)
set_target_properties(serialize PROPERTIES SUFFIX ".so")
