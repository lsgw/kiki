set(src
	udp.cpp
	tcp.cpp
)
add_library(driver SHARED ${src})
set_target_properties(driver PROPERTIES SUFFIX ".so")

add_subdirectory(dns)
add_subdirectory(exe)
add_subdirectory(http)