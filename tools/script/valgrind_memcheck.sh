#!/usr/bin/env bash

valgrind --leak-check=full --tool=memcheck --log-file=memcheck.log --malloc-fill=0x5E "$@";
