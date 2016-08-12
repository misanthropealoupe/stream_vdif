all: stream_vdif

stream_vdif: stream_vdif.cpp
	g++ -std=c++11 -o stream_vdif -pthread -lpthread $^
