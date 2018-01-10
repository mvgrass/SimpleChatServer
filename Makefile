all: build_server

build_server:
	g++ -std=c++11 -ggdb -O0 -o server server.cpp
