#!/bin/bash

#
#	parse args:
#		--flags X,Y,Z	-> adds -DX -DY -DZ to g++ (comma-separated, one --flags arg)
#		(omitted)	-> compile without -D flags
#
#	usage:
#		./build.sh
#		./build.sh --flags REALLOCATE_WITH_COPY
#		./build.sh --flags REALLOCATE_WITH_COPY,NDEBUG
#
FLAGS_RAW=""
while [[ $# -gt 0 ]]; do
	case "$1" in
		--flags)
			FLAGS_RAW="$2"
			shift 2
			;;
		*)
			shift
			;;
	esac
done

DEFINE_FLAGS=""
if [ -n "$FLAGS_RAW" ]; then
	IFS=',' read -ra FLAG_ARRAY <<< "$FLAGS_RAW"
	for flag in "${FLAG_ARRAY[@]}"; do
		DEFINE_FLAGS="$DEFINE_FLAGS -D$flag"
	done
	echo "compiling with: $DEFINE_FLAGS"
else
	echo "no flags specified, compiling without -D"
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
	$DEFINE_FLAGS	\
	main.cpp	\
	-o main
