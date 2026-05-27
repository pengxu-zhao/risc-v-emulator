#include "gdb.h"
#include <unistd.h>
extern Bus bus;
int gdb_fd;
int gdb_send_packet(int fd, const char *data)
{
    unsigned char csum = 0;
    char buf[4096];

    int len = strlen(data);
    for (int i = 0; i < len; i++)
        csum += data[i];

    sprintf(buf, "$%s#%02x", data, csum & 0xff);
    return write(fd, buf, strlen(buf));
}

int gdb_recv_packet(int fd, char *buf)
{
    char c;
    unsigned char csum;
    char recv_checksum[3] = {0};

    while (1)
    {
        int i = 0;
        csum = 0;

        while (1)
        {
            if (read(fd, &c, 1) != 1)
                return -1;
            if (c == '$')
                break;
            if (c == '\x03')
                continue; // ignore Ctrl-C for now
        }

        while (read(fd, &c, 1) == 1)
        {
            if (c == '#')
                break;
            buf[i++] = c;
        }
        buf[i] = 0;

        if (read(fd, &recv_checksum[0], 1) != 1 || read(fd, &recv_checksum[1], 1) != 1)
            return -1;

        recv_checksum[2] = 0;
        for (int j = 0; j < i; j++)
            csum += buf[j];

        unsigned int expected = strtol(recv_checksum, NULL, 16);
        if ((csum & 0xff) != expected)
        {
            write(fd, "-", 1);
            continue;
        }

        write(fd, "+", 1);
        return i;
    }
}

void handle_g(int fd, CPU_State *cpu)
{
    printf("[GDB] handling 'g' command (get all registers)\n");
    fflush(stdout);
    
    pthread_mutex_lock(&cpu->lock);

    char out[4096] = {0};
    char tmp[32];

    for (int i = 0; i < 32; i++)
    {
        uint64_t val = cpu->gpr[i];
        for (int b = 0; b < 8; b++)
        {
            sprintf(tmp, "%02x", (val >> (8 * b)) & 0xff);
            strcat(out, tmp);
        }
    }

    uint64_t pc = cpu->pc;
    for (int b = 0; b < 8; b++)
    {
        sprintf(tmp, "%02x", (pc >> (8 * b)) & 0xff);
        strcat(out, tmp);
    }

    pthread_mutex_unlock(&cpu->lock);

    printf("[GDB] sending registers: %s\n", out);
    fflush(stdout);
    gdb_send_packet(fd, out);
}
void handle_m(int fd, CPU_State *cpu, char *pkt)
{
    uint64_t addr = 0;
    int len = 0;

    int n = sscanf(pkt + 1, "%lx,%d", &addr, &len);
    if (n != 2)
    {
        printf("[GDB] parse error: %s\n", pkt);
        return;
    }

    if (len <= 0 || len > 1024)
    {
        printf("[GDB] invalid mem length: %d\n", len);
        return;
    }

    printf("[GDB] mem read addr=0x%lx len=%d\n", addr, len);

    pthread_mutex_lock(&cpu->lock);

    char out[4096] = {0};
    int off = 0;

    for (int i = 0; i < len; i++)
    {
        uint8_t val = bus_read(&bus, addr + i, 1);
        off += snprintf(out + off, sizeof(out) - off, "%02x", val);
        if (off >= (int)sizeof(out) - 4)
            break;
    }

    pthread_mutex_unlock(&cpu->lock);

    gdb_send_packet(fd, out);
}

void gdb_send_stop_reply(CPU_State *cpu)
{
    char buf[128];

    // GDB 停止回复格式：T signal [register=value;]*
    // T05 表示 SIGTRAP (5)
    // thread:01 表示线程 ID 1
    // pc:xxxxxxxx 表示 PC 寄存器值
    // 对于 RISC-V，PC 寄存器编号是 65 (0x41)
    snprintf(buf, sizeof(buf), "T05thread:01;41:%016lx;", cpu->pc);

    printf("[GDB SEND] stop reply: %s\n", buf);
    fflush(stdout);

    gdb_send_packet(gdb_fd, buf);
}

void *gdb_thread(void *arg)
{
    CPU_State *cpu = (CPU_State *)arg;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("bind failed");
        exit(1);
    }

    if (listen(server_fd, 1) < 0)
    {
        perror("listen failed");
        exit(1);
    }

    printf("[GDB] waiting on %d...\n", PORT);
    fflush(stdout);  // 确保立即打印

    gdb_fd = accept(server_fd, NULL, NULL);

    printf("[GDB] connected\n");
    fflush(stdout);  // 确保立即打印

    char pkt[1024];

    while (1)
    {
        //printf("[GDB] recv loop\n");
        fflush(stdout);
        int pkt_len = gdb_recv_packet(gdb_fd, pkt);
        if (pkt_len <= 0)
        {
            printf("[GDB] connection closed or packet error\n");
            fflush(stdout);
            break;
        }

        printf("[GDB] received packet: '%s' (len=%d)\n", pkt, pkt_len);
        fflush(stdout);

        if (strcmp(pkt, "?") == 0)
        {
            printf("[GDB] sending stop reply\n");
            fflush(stdout);
            gdb_send_packet(gdb_fd, "S05");
        }
        else if (pkt[0] == 'g')
        {
            printf("[GDB] 'g' command - get registers\n");
            fflush(stdout);
            handle_g(gdb_fd, cpu);
        }
        else if (pkt[0] == 'm')
        {
            printf("[GDB] 'm' command - read memory\n");
            fflush(stdout);
            handle_m(gdb_fd, cpu, pkt);
        }
        else if (pkt[0] == 's')
        {
            printf("[GDB] 's' command - single step\n");
            fflush(stdout);
            
            // 发送确认响应
            gdb_send_packet(gdb_fd, "OK");
            
            pthread_mutex_lock(&cpu->lock);
            cpu->single_step = 1;
            cpu->state = CPU_RUNNING;
            pthread_cond_signal(&cpu->cond);
            pthread_mutex_unlock(&cpu->lock);
        }
        else if (pkt[0] == 'c')
        {
            printf("[GDB] 'c' command - continue\n");
            fflush(stdout);
            pthread_mutex_lock(&cpu->lock);
            cpu->state = CPU_RUNNING;
            cpu->stop_sent = 0;
            pthread_cond_signal(&cpu->cond);
            pthread_mutex_unlock(&cpu->lock);
        }
        else if (pkt[0] == 'q')
        {
            printf("[GDB] 'q' command - query\n");
            fflush(stdout);
            gdb_send_packet(gdb_fd, "");
        }
        else
        {
            printf("[GDB] unknown command: '%s'\n", pkt);
            fflush(stdout);
            gdb_send_packet(gdb_fd, "");
        }
    }
}
