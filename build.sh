#!/bin/bash

gcc -g -I. -Iinclude -DCONFIG_INT128 host-utils.c int128.c softfloat.c fpu_helper.c vec_helper.c tlb_helper.c interpreter.c main.c -lm -o la_emu


