## LoongArch Emulator

### Build

```
make -j
debug support
make DEBUG=1 -j
gdb support
make GDB=1 -j
clean
make clean
```

## Usagse

### User Mode
- `./build/la_emu_user [options] program [arguments...]`

### Kernel Mode
- `./build/la_emu_kernel -m 16 -k vmlinux`

Download execuable binary from [here](https://github.com/rrwhx/binary_resource).

### Options

|  Option | Function  |
|---|---|
| -d   | Log info, suupport: exec,cpu,fpu,int  |
| -D   | Log file  |
| -z                 | Determined events  |
| -g                | Enable gdbserver  |
| -m                | Memory size(kernel mode)  |
| -k                | Kernel vmlinux(kernel mode)  |

## Features

|  Features | Status  |
|---|---|
| Loongarch64 base   | &check;  |
| Loongarch64 privilege   | &check;  |
| FP                 | &check;  |
| LSX                | &check;  |
| LASX                | &check;  |
| Timer              | &check;  |
| Serial Port        | &check;  |
| Gdb Server         | &check;  |
| Determined events  | &check;  |
| All SPEC CPU       | &check;  |
| Dynamic ELF        | &cross;  |
| Multithread        | &cross;  |
| Signal             | &cross;  |
| Block Device       | &cross;  |


## Performance
    Up to 3% of native.
