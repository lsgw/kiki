set(src
	base/CountDownLatch.cpp
	base/Thread.cpp
	base/sockets.cpp
	base/Buffer.cpp
	base/utils.cpp

	Env.cpp
	Timer.cpp
	Monitor.cpp
	Module.cpp
	Context.cpp
	Channel.cpp
	Selector.cpp
	EventLoop.cpp
	WorkerLoop.cpp
	Reactor.cpp
	Main.cpp
)

add_executable(targetssnet ${src})

IF (APPLE)
	target_link_libraries(targetssnet lua)
	target_link_libraries(targetssnet pthread m dl)
	set_target_properties(targetssnet PROPERTIES LINK_FLAGS "-Wl,-all_load")
ELSE()
	target_link_libraries(targetssnet -Wl,--whole-archive lua -Wl,--no-whole-archive)
	target_link_libraries(targetssnet pthread m dl rt)
	set_target_properties(targetssnet PROPERTIES LINK_FLAGS "-Wl,-E")
ENDIF(APPLE)
