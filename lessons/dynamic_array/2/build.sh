#!/bin/bash

#
#	parse args:
#		--block <NAME>	-> adds -D<NAME> to g++
#		(omitted)	-> compile without -D flags
#		--block null	-> explicit null guard, compile without -D flags
#
#	usage:
#		./build.sh
#		./build.sh --block null
#		./build.sh --block BLOCK_3
#
BLOCK=""
while [[ $# -gt 0 ]]; do
	case "$1" in
		--block)
			BLOCK="$2"
			shift 2
			;;
		*)
			shift
			;;
	esac
done

DEFINE_FLAG=""
if [ -z "$BLOCK" ] || [ "$BLOCK" == "null" ]; then
	echo "no block specified, compiling without -D flags"
else
	DEFINE_FLAG="-D$BLOCK"
	echo "compiling with $DEFINE_FLAG"
fi

if [ -f main ]; then
	rm -f main
fi

g++ \
	-std=c++23	\
	-O2	\
	-Wall	\
	-Wextra	\
	-Wpedantic	\
	$DEFINE_FLAG	\
	main.cpp	\
	-o main
