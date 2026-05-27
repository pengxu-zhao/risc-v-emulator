#ifndef GDB_H
#define GDB_H

#include "common.h"
#include "cpu.h"
#include "memory.h"
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
void* gdb_thread(void *arg);
void gdb_send_stop_reply(CPU_State *cpu);
#endif