#!/bin/sh

valgrind --leak-check=full --tool=memcheck --show-leak-kinds=all --log-file=memcheck.log --malloc-fill=0x5E "$@";
