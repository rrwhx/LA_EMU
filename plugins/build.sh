#!/bin/sh -e
set -x
g++ -fPIC -shared -o liblog_exec.so log_exec.cc
