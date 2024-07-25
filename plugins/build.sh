#!/bin/sh -e
set -x
g++ -g -O2 -Werror -fPIC -shared -o liblog_exec.so log_exec.cc
g++ -g -O2 -Werror -fPIC -shared -o libbbv.so bbv.cc
