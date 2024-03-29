#include "qemu/osdep.h"

#include <stddef.h>
#include <time.h>
#include <float.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include <errno.h>

#include <pthread.h>
#include <sched.h>

#include <elf.h>
#include <unistd.h>
#include <fcntl.h>
#include <execinfo.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/auxv.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "cpu.h"

#include "gdbserver.h"

bool gdb_verbose;

uint64_t gdb_fetch_breakpoint[GDB_FETCCH_BREAKPOINT_NUM];

int sock_fd = -1;
static const char hexchars[] = "0123456789abcdef";
volatile int gdbserver_has_message;
static void io_handler(int sig) {
    // fprintf(stderr, "signal %d %s\n", sig, strsignal(sig));
    gdbserver_has_message = 1;
}

void remote_prepare(int port) {
    int ret;
    struct sockaddr_in addr;
    const int one = 1;
    int listen_fd;

    listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    if (listen_fd < 0)
    {
        perror("socket() failed");
        exit(-1);
    }

    ret = setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (ret < 0)
    {
        perror("setsockopt() failed");
        exit(-1);
    }

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    ret = bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0)
    {
        perror("bind() failed");
        exit(-1);
    }

    ret = listen(listen_fd, 1);
    if (ret < 0)
    {
        perror("listen() failed");
        exit(-1);
    }

    fprintf(stderr, "Listening on port %d\n", port);

    sock_fd = accept(listen_fd, NULL, NULL);
    if (sock_fd < 0)
    {
        perror("accept() failed");
        exit(-1);
    } else {
        fprintf(stderr, "sock_fd:%d\n", sock_fd);
    }

    ret = setsockopt(sock_fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
    ret = setsockopt(sock_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    // enable_async_notification(sock_fd);
    close(listen_fd);
}

static int hexval(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return 0;
}

uint8_t gdb_sum(const void* buf, size_t len) {
    const uint8_t* p = buf;
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) {
        sum += p[i];
    }
    return sum;
}

void convert_u64(uint64_t data, char* buf, size_t index) {
    for (size_t i = 0; i < 8; i++) {
        buf[index + i * 2] = hexchars[(data >> (i * 8 + 4)) & 0xf];
        buf[index + i * 2 + 1] = hexchars[(data >> (i * 8    )) & 0xf];
    }
}

int str_startswith(const char* str, const char* start) {
    return !strncmp(str, start, strlen(start));
}

void dump_nchar(char* str, int n) {
    for (int i = 0; i < n; i++) {
        fprintf(stderr, "%c", str[i]);
    }
    fprintf(stderr, "\n");
}

#define SEND_BUF_SIZE 0x100000
#define RECEIVE_BUF_SIZE 0x100000
static char send_buf[SEND_BUF_SIZE];
static char send_buf_tmp[RECEIVE_BUF_SIZE];
static char receive_buf[RECEIVE_BUF_SIZE];
ssize_t receive_buf_bytes = 0;

const char* ack_response = "+";
const char* empty_response = "$#00";

int send_message_raw(int sock_fd, const char* message, ssize_t message_len) {
    if (message_len < 0) {
        message_len = strlen(message);
    }
    ssize_t send_bytes = write(sock_fd, message, message_len);
    assert(send_bytes == message_len);

    fprintf(stderr, "send %ld bytes ", message_len);
    for (int i = 0; i < message_len; i++)
    {
        fprintf(stderr, "%c", message[i]);
    }
    fprintf(stderr, "\n");

    return 0;
}

int send_message_n(int sock_fd, const char* message, ssize_t message_len) {
    if (message_len < 0) {
        message_len = strlen(message);
    }
    assert(message_len + 4 < 0x100000);
    uint8_t check_sum = gdb_sum(message, message_len);
    send_buf[0] = '$';
    for (ssize_t i = 0; i < message_len; i++)
    {
        send_buf[i + 1] = message[i];
    }
    send_buf[message_len + 1] = '#';

    send_buf[message_len + 2] = hexchars[check_sum >> 4];
    send_buf[message_len + 3] = hexchars[check_sum & 0xf];
    send_message_raw(sock_fd, send_buf, message_len + 4);
    return 0;
}

int send_message(int sock_fd, const char* message) {
    send_message_n(sock_fd, message, -1);
    return 0;
}

int send_message_ack(int sock_fd) {
    send_message_raw(sock_fd, ack_response, -1);
    return 0;
}

int send_message_empty(int sock_fd) {
    send_message_raw(sock_fd, empty_response, -1);
    return 0;
}

int send_message_ack_empty(int sock_fd) {
    send_message_ack(sock_fd);
    send_message_empty(sock_fd);
    return 0;
}

int send_message_ack_message(int sock_fd, const char* message) {
    send_message_ack(sock_fd);
    send_message(sock_fd, message);
    return 0;
}
bool cpu_can_run;

void gdbserver_read_message() {
    while (gdbserver_has_message) {
        gdbserver_has_message = 0;
        ssize_t r = read(sock_fd, receive_buf + receive_buf_bytes, 0x10000 - receive_buf_bytes);
        if (r < 0) {
            if (gdb_verbose) fprintf(stderr, "read failed %ld\n", r);
        } else if (r == 0) {
            if (gdb_verbose) fprintf(stderr, "read nothing\n");
            continue;
        } else {
            if (gdb_verbose) {
                fprintf(stderr, "read %ld bytes\n", r);
                dump_nchar(receive_buf + receive_buf_bytes, r);
                fprintf(stderr, "current receive_buf\n");
                dump_nchar(receive_buf, receive_buf_bytes + r);
            }
            receive_buf_bytes += r;
        }
    }
}
void gdbserver_handle_message() {
    while (receive_buf_bytes && (receive_buf[0] == '+' || receive_buf[0] == 3)) {
        if (receive_buf[0] == 3) {
            fprintf(stderr, "Remote Interrupt\n");
            cpu_can_run = false;
            send_message(sock_fd, "S02");
        }
        memmove(receive_buf, receive_buf + 1, receive_buf_bytes - 1);
        -- receive_buf_bytes;
    }
    if (!receive_buf_bytes) {
        return;
    }
    if(receive_buf[0] != '$') {
        fprintf(stderr, "lxy: %s:%d %s does not begin with $\n", __FILE__,__LINE__,__func__);
        for (int i = 0; i < receive_buf_bytes + 2; i++) {
            fprintf(stderr, "%c", receive_buf[i]);
        }
        fprintf(stderr, "\n");
        exit(0);
    }
    char *packetend_ptr = memchr(receive_buf, '#', receive_buf_bytes);
    if(!packetend_ptr) {
        return;
    }
    int packetsize = packetend_ptr - receive_buf;
    if (packetsize + 3 > receive_buf_bytes) {
        return;
    } else {
        uint8_t checksum = gdb_sum(receive_buf + 1, packetsize - 1);
        uint8_t receive_checksum = (hexval(receive_buf[packetsize + 1]) << 4 | hexval(receive_buf[packetsize + 2]));
        if (checksum != receive_checksum) {
            fprintf(stderr, "checksum mismatch,checksum:%02x receive_checksum:%02x\n", checksum, receive_checksum);
            for (int i = 0; i < packetsize + 2; i++)
            {
                fprintf(stderr, "%c", receive_buf[i]);
            }
            fprintf(stderr, "\n");
            exit(0);
        }
    }
    receive_buf[packetsize] = '\0';
    packetsize += 3;
    char request = receive_buf[1];
    char *payload = (char *)&receive_buf[2];
    int payload_size = packetsize - 4;
    fprintf(stderr, "receive packet: cmd :%c, ", request);
    dump_nchar(receive_buf, packetsize);
    switch (request) {
        case '?' : {
            send_message_ack(sock_fd);
            send_message(sock_fd, "S05");
            }
            break;
        case 'g' : {
                for (size_t i = 0; i < 32; i++) {
                    convert_u64(current_env->gpr[i], send_buf_tmp, i * 16);
                }
                // new world
                convert_u64(0x1234567812345678, send_buf_tmp, 32 * 16);
                convert_u64(current_env->pc,    send_buf_tmp, 33 * 16);
                convert_u64(0x1234567812345678, send_buf_tmp, 34 * 16);
                send_message_ack(sock_fd);
                send_message_n(sock_fd, send_buf_tmp, 35 * 16);

                // old world
                // convert_u64(current_env->pc,    send_buf_tmp, 32 * 16);
                // convert_u64(0x1234567812345678, send_buf_tmp, 33 * 16);
                // send_message_ack(sock_fd);
                // send_message_n(sock_fd, send_buf_tmp, 34 * 16);
            }
            break;
        case 'm' : {
                uint64_t maddr, mlen;
                assert(sscanf(payload, "%lx,%lx", &maddr, &mlen) == 2);
                if (maddr == 0  || maddr == 0xfffffffffffffffc) {
                    send_message_ack(sock_fd);
                    send_message(sock_fd, "E14");
                } else {
                    for (int i = 0; i < mlen; i++) {
                        uint8_t t = ram_ldub(maddr + i);
                        send_buf_tmp[i * 2] = hexchars[t >> 4];
                        send_buf_tmp[i * 2 + 1] = hexchars[t & 0xf];
                    }
                    send_message_ack(sock_fd);
                    send_message_n(sock_fd, send_buf_tmp, mlen * 2);
                }
            }
            break;
        case 'Z' : {
                uint64_t type, addr, length;
                assert(sscanf(payload, "%lx,%lx,%lx", &type, &addr, &length) == 3);
                if (type == 0) {
                    bool ok = false;
                    for (int i = 0; i < GDB_FETCCH_BREAKPOINT_NUM; i++) {
                        if (gdb_fetch_breakpoint[i] == 0) {
                            gdb_fetch_breakpoint[i] = addr;
                            ok = true;
                            break;
                        }
                    }
                    send_message_ack(sock_fd);
                    if (ok) {
                        send_message(sock_fd, "OK");
                    } else {
                        send_message_empty(sock_fd);
                    }
                } else {
                    fprintf(stderr, "Z %c\n", (uint8_t)type);
                    send_message_ack_empty(sock_fd);
                }
            }
            break;
        case 'z' : {
                uint64_t type, addr, length;
                assert(sscanf(payload, "%lx,%lx,%lx", &type, &addr, &length) == 3);
                if (type == 0) {
                    for (int i = 0; i < GDB_FETCCH_BREAKPOINT_NUM; i++) {
                        if (gdb_fetch_breakpoint[i] == addr) {
                            gdb_fetch_breakpoint[i] = 0;
                        }
                    }
                    send_message_ack_message(sock_fd, "OK");
                } else {
                    fprintf(stderr, "z %c\n", (uint8_t)type);
                    send_message_ack_empty(sock_fd);
                }
            }
            break;
        case 'c' : {
                // uint64_t addr;
                // assert(sscanf(payload, "%lx,%lx,%lx", &type, &addr, &length) == 3);
                if (payload_size <= 2) {
                    send_message_ack_message(sock_fd, "OK");
                    cpu_can_run = true;
                } else {
                    fprintf(stderr, "lxy: %s:%d %s \n", __FILE__,__LINE__,__func__);
                    send_message_ack_empty(sock_fd);
                }
            }
            break;
        case 'H' : {
                send_message_ack_empty(sock_fd);
                // uint64_t addr;
                // // assert(sscanf(payload, "%lx,%lx,%lx", &type, &addr, &length) == 3);
                // if (payload[0] == 'c') {
                //     // uint64_t addr;
                //     // assert(sscanf(payload, "%lx,%lx,%lx", &type, &addr, &length) == 3);
                //     if (payload_size <= 2) {
                //         send_message(sock_fd, "OK");
                //         return 0;
                //     } else {
                //         fprintf(stderr, "lxy: %s:%d %s \n", __FILE__,__LINE__,__func__);
                //         send_message_ack_empty(sock_fd);
                //     }
                // } else {
                //     fprintf(stderr, "lxy: %s:%d %s \n", __FILE__,__LINE__,__func__);
                //     send_message_ack_empty(sock_fd);
                // }
            }
            break;
        case 'q' : {
                // uint64_t addr;
                if (payload[0] == 'C') {
                    send_message_ack(sock_fd);
                    send_message(sock_fd, "qC2222");
                } else if (str_startswith(payload, "Attached")) {
                    send_message_ack(sock_fd);
                    send_message(sock_fd, "0");
                } else {
                    fprintf(stderr, "lxy: %s:%d %s \n", __FILE__,__LINE__,__func__);
                    send_message_ack_empty(sock_fd);
                }
            }
            break;
        case 'k' : {
                fprintf(stderr, "quit\n");
                exit(0);
            }
            break;
        default:
            send_message_ack_empty(sock_fd);
    }
    memmove(receive_buf, packetend_ptr + 3, packetsize);
    receive_buf_bytes -= packetsize;
}

int gdbserver_loop(void) {
    int r = 0;
    while (1) {
        gdbserver_read_message();
        gdbserver_handle_message();
        if (cpu_can_run) {
            int r = exec_env(current_env);
            fprintf(stderr, "gdb_exec_env exit %d\n", r);
            if (r == 2) {
                send_message(sock_fd, "S05");
                cpu_can_run = false;
            }
        }
    }
    return r;
}

void gdbserver_init(int port) {
    remote_prepare(port);
    signal(SIGIO, io_handler);
    /* Set the process receiving SIGIO/SIGURG signals to us. */
    if (fcntl(sock_fd, F_SETOWN, getpid()) < 0) {
        perror("fcntl F_SETOWN");
        exit(1);
    }
    if (fcntl(sock_fd, F_SETFL, FASYNC | O_NONBLOCK) < 0) {
        perror("fcntl FASYNC");
        exit(1);
    }
}

