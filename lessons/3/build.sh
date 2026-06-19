#!/bin/bash

if [ -f main ]; then
	rm -f main
fi

g++ \
	-std=c++23	\
	-O2	\
	-Wall	\
	-Wextra	\
	-Wpedantic	\
	main.cpp	\
	-o main
