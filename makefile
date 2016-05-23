all: main.cpp httpserver.hpp
	g++  main.cpp -std=c++11 -I /opt/local/include/ -lboost_system-mt -o httpserver -g
