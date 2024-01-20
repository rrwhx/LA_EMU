#!/bin/bash

gcc -Iinclude -DCONFIG_INT128 tlb_helper.c interpreter.c main.c -g -o la_emu


