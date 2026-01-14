// uart_device.c
// Simple memory-mapped UART device for a RISC-V simulator.
// Compile with: gcc -std=gnu11 -pthread -O2 uart_device.c -o uart_test
// This file provides a uart_create(...) function and MMIO handlers
// Example integration notes are below the implementation.

#include "uart.h"
#include "cpu.h"
#include <ctype.h>

static void uart_update_lsr(UARTDevice *u) {
    u->lsr = 0;
    if (u->rx_count > 0) u->lsr |= LSR_DR;
    if (u->tx_count == 0) u->lsr |= LSR_THRE | LSR_TEMT;
    // keep OE if set by overflow
    if (u->tx_count > 0) u->lsr &= ~(LSR_THRE | LSR_TEMT);
}

static void uart_set_overrun(UARTDevice *u) {
    u->lsr |= LSR_OE;
}

void uart_update_irq_old(UARTDevice* uart) {

    pthread_mutex_lock(&uart->lock);

    int raise_irq = 0;
    int irq_type = 0;
   
    // 全局中断使能检查 (MCR OUT2)
    if (!(uart->mcr & 0x08)) {
        // 全局中断禁用，清除所有中断
        if (uart->irq_cb) {
            uart->irq_cb(uart->cpu_opaque, 0, uart->irq_num);
        }
        uart->iir = 0x01; // 无中断挂起
        pthread_mutex_unlock(&uart->lock);

        return;
    }else{

    }
    // 检查接收数据中断 (IER bit0)
    if ((uart->ier & 0x01) && (uart->rx_count > 0)) {
        raise_irq = 1;
        irq_type = 0x04; // 接收数据可用
        uart->iir = irq_type;
 
    }
    // 检查发送保持寄存器空中断 (IER bit1)
    else if ((uart->ier & 0x02) && (uart->lsr & 0x20)) {
        raise_irq = 1;
        irq_type = 0x02; // 发送保持寄存器空
        uart->iir = irq_type;

    }
    // 检查线路状态中断 (IER bit2)
    else if ((uart->ier & 0x04) && (uart->lsr & 0x1E)) { // 任何错误条件
        raise_irq = 1;
        irq_type = 0x06; // 接收线路状态
        uart->iir = irq_type;

    }
    // 检查 Modem 状态中断 (IER bit3)
    else if ((uart->ier & 0x08) && (uart->msr & 0x0F)) { // 任何 Modem 状态变化
        raise_irq = 1;
        irq_type = 0x00; // Modem 状态变化
        uart->iir = irq_type;

    }
    else {
        uart->iir = 0x01; // 无中断挂起

    }

    // 调用中断回调
 
    if (uart->irq_cb) {
        uart->irq_cb(uart->cpu_opaque, raise_irq, uart->irq_num);
    }else{
        printf("[UART] uart_update_irq: No IRQ callback registered\n");
        fflush(stdout);
    }
    pthread_mutex_unlock(&uart->lock);

}


/* ---------- utility (circular buffer) ---------- */
static inline bool tx_buf_is_empty(UARTDevice *u) { return u->tx_count == 0; }
static inline bool tx_buf_is_full(UARTDevice *u)  { return u->tx_count >= UART_TX_BUF_SIZE; }
static inline bool rx_buf_is_empty(UARTDevice *u) { return u->rx_count == 0; }
static inline bool rx_buf_is_full(UARTDevice *u)  { return u->rx_count >= UART_RX_BUF_SIZE; }

static bool tx_buf_push(UARTDevice *u, uint8_t b) {

    // 检查锁状态（我们已经在锁内）

    bool ret = !tx_buf_is_full(u);

    // 检查缓冲区状态

    if (!ret) {

        // policy: drop oldest (advance tail)
        u->tx_tail = (u->tx_tail + 1) % UART_TX_BUF_SIZE;
        u->tx_count--;

    }
    if(u->tx_count >= UART_TX_BUF_SIZE){
        pthread_mutex_unlock(&u->lock);
        return false;
    }

    u->tx_buf[u->tx_head] = b;

    u->tx_head = (u->tx_head + 1) % UART_TX_BUF_SIZE;
    u->tx_count++;
    uart_update_lsr(u);

    //if(tx_buf_is_empty(u))
    { 

        pthread_cond_signal(&u->tx_cond); 
    }

    return ret;
}

static bool tx_buf_pop(UARTDevice *u, uint8_t *out) {
    
    if (tx_buf_is_empty(u)) return false;
    *out = u->tx_buf[u->tx_tail];
    u->tx_tail = (u->tx_tail + 1) % UART_TX_BUF_SIZE;
    u->tx_count--;
    
    uart_update_lsr(u);
    return true;
}

static void rx_buf_push(UARTDevice *u, uint8_t b) {
    pthread_mutex_lock(&u->lock);
    if (rx_buf_is_full(u)) {
        // drop oldest policy
        u->rx_tail = (u->rx_tail + 1) % UART_RX_BUF_SIZE;
        u->rx_count--;
        u->lsr |= LSR_OE;
    }
    u->rx_buf[u->rx_head] = b;
    u->rx_head = (u->rx_head + 1) % UART_RX_BUF_SIZE;
    u->rx_count++;
    u->lsr |= LSR_DR; //设置数据就绪位
    pthread_mutex_unlock(&u->lock);
    uart_update_irq(u);
}

static bool rx_buf_pop(UARTDevice *u, uint8_t *out) {
    pthread_mutex_lock(&u->lock);
    bool ret = false;
    if (rx_buf_is_empty(u)) ret = false;
    *out = u->rx_buf[u->rx_tail];
    u->rx_tail = (u->rx_tail + 1) % UART_RX_BUF_SIZE;
    u->rx_count--;
    uart_update_lsr(u);
    pthread_mutex_unlock(&u->lock);
    ret = true;
    uart_update_irq(u);
    return ret;
}

/* ---------- IRQ helper ---------- */
static void uart_maybe_raise_irq(UARTDevice *u) {
    // called with lock held
    int pending = 0;
    if (!rx_buf_is_empty(u) && (u->ctrl & UART_CTRL_RX_INT_EN)) pending |= UART_IRQ_RX_PENDING;
    if (tx_buf_is_empty(u) && (u->ctrl & UART_CTRL_TX_INT_EN)) pending |= UART_IRQ_TX_PENDING;

    // set irq_status bits that are pending
    uint32_t new_pending = u->irq_status | pending;
    if (new_pending != u->irq_status) {
        u->irq_status = new_pending;
        // raise external IRQ line
        if (u->irq_cb) {
            // raise = 1 to assert; irq_num identifies line
            u->irq_cb(u->cpu_opaque, 1, u->irq_num);
        }
    }
}

static void uart_maybe_clear_irq(UARTDevice *u) {
    // called with lock held
    if (u->irq_status == 0) {
        if (u->irq_cb) u->irq_cb(u->cpu_opaque, 0, u->irq_num);
    }
}

/* ---------- TX thread: consumes tx_buf and writes to stdout ---------- */
static void *uart_tx_thread(void *arg) {
    UARTDevice *u = (UARTDevice *)arg;
    while (1) {

        pthread_mutex_lock(&u->lock);
        while (u->running && tx_buf_is_empty(u)) {
            // wait until there's data or we're shutting down
            pthread_cond_wait(&u->tx_cond, &u->lock);
        }
        if (!u->running && tx_buf_is_empty(u)) {
            pthread_mutex_unlock(&u->lock);
            break;
        }
        
        uint8_t b;
        bool ok = tx_buf_pop(u, &b);


       /* if(ok){
            ssize_t written = write(u->serial_fd, &b, 1);
            if(written != 1) perror("Uart TX write failed\n");
            usleep(87);// 模拟波特率延迟 (115200 baud = ~87μs per byte)  
        } */

      //  printf("[uart]ok:%d b:%d\n",ok,b);
        // update IRQ state before unlocking
        //uart_maybe_raise_irq(u);
        
        pthread_mutex_unlock(&u->lock);

        if(!ok) continue;
        write(STDOUT_FILENO, &b, 1);
        if(b == '\n') fflush(stdout);
        usleep(87);// 模拟波特率延迟 (115200 baud = ~87μs per byte)  
        uart_update_irq(u);
    }

    if (u->serial_fd >= 0) {
        close(u->serial_fd);
        u->serial_fd = -1;
    }
    return NULL;
}

/* ---------- RX thread: reads from stdin and pushes to rx_buf ---------- */
static void *uart_rx_thread(void *arg) {
    UARTDevice *u = (UARTDevice *)arg;

    int input_fd = STDIN_FILENO;

       // 设置stdin为非阻塞
    int flags = fcntl(input_fd, F_GETFL, 0);
    fcntl(input_fd, F_SETFL, flags | O_NONBLOCK);

    // set stdin non-blocking? We'll use blocking read in a loop to keep things simple.
    while (u->running) {
        uint8_t buf[128];
        ssize_t r = read(input_fd, buf, sizeof(buf));
        
        if (r <= 0) {
            if (r == 0) {
                // EOF -> stop reading
                break;
            }
            if (errno == EINTR) continue;
            // If no data and blocking, sleep briefly
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("[UART] read error");  // 记录真实错误
            }
            usleep(10000);
            continue;
        }
        for (ssize_t i = 0; i < r; ++i) 
        {
            // 特殊处理：如果按Ctrl+D (EOF)
            if (buf[i] == 0x04) {
                printf("\n[UART] EOF (Ctrl+D) received\n");
                continue;
            }
            rx_buf_push(u, buf[i]);
         
        }
      
        //uart_maybe_raise_irq(u); // may set RX irq

    }
    return NULL;
}

/* ---------- MMIO read/write handlers ---------- */
/* size is in bytes; offset is addr - base_addr */
uint32_t uart_mmio_read(UARTDevice *u, uint64_t offset, unsigned size) {
    uint32_t res = 0;
    uint8_t value = 0;
    bool dlab = (u->lcr & 0x80) != 0;
    
    pthread_mutex_lock(&u->lock);
    switch (offset) {// cpu access addr
        case UART_REG_DATA: { // rbr/thr
            if(dlab){ //访问 DLL,DLL 用于设置波特率（Baud rate）
                // 波特率除数锁存器 (LSB)
               return u->dll;
            }else{
                uint8_t b = 0;
                if (rx_buf_pop(u, &b)) {
                    res = (uint32_t)b;
                    u->lsr &= ~LSR_OE;
                    uart_update_irq(u);
                } else {
                    res = 0; // or 0: choose convention (we choose 0xFFFFFFFF to indicate empty)
                }
                // after popping, irq might clear
                //16550
                /* soc
                if (u->rx_count == 0) {
                    // clear RX pending bit
                    u->irq_status &= ~UART_IRQ_RX_PENDING;
                    if (u->irq_status == 0 && u->irq_cb) u->irq_cb(u->cpu_opaque, 0, u->irq_num);
                }*/
            }
            break;
        }
        case 1:
        {   
            if(dlab){
                res = u->dlm;
            }else{
                res = u->ier;
            }
            break;
        }
        case 2:
        {
            res = u->iir;
            // 如果有接收数据中断
            if ((u->ier & 0x01) && (u->rx_count > 0)) {
            res = 0x04; // 接收数据可用中断
            }
            break;
        }
        case 3:
        {
            res = u->lcr;
            break;
        }
        case UART_REG_STATUS: {  // mcr
     
            res = u->mcr;
            break;
        }
        case 5: //lsr
        {
            res = u->lsr;
            break;
        }
        case 6: //msr
        {
            res = u->msr;
            break;
        }
        case 7: //scr
        {
            res = u->scr;
            break;
        }
        case UART_REG_CTRL:
            res = u->ctrl;
            break;
        case UART_REG_IRQ_STATUS:
            res = u->irq_status;
            break;
        case UART_REG_CONFIG:
            res = 0;
            break;
        default:
            res = 0xFFFFFFFFu;
            break;
    }
    pthread_mutex_unlock(&u->lock);
    // adjust to requested size (support 1/2/4 byte reads)
    if (size == 1) return res & 0xFF;
    if (size == 2) return res & 0xFFFF;
    return res;
}

void uart_update_baud(UARTDevice *uart) {
    uint16_t divisor = (uart->dlm << 8) | uart->dll;
    if (divisor != 0)
        uart->baud_rate = 1843200 / (16 * divisor);
    else
        uart->baud_rate = 1843200 / 16;
}

void uart_mmio_write(UARTDevice *u, uint64_t offset, uint32_t val, unsigned size) {

    bool need_irq_update = false;
    pthread_mutex_lock(&u->lock);
    
    // normalize val to 32-bit
    uint32_t v = val;
    bool dlab = (u->lcr & 0x80) != 0;

    // 对 tx_buf_push 或 tx_buf_is_empty 显式加锁
    // TX 线程或其他线程并发访问 u->tx_buf

    switch (offset) {
        case UART_REG_DATA: {
            if(dlab){
                u->dll = val;
                uart_update_baud(u);
            }else{
                uint8_t b = (uint8_t)(v & 0xFF);
                char c = val & 0xFF;
            
                //fprintf(stderr, "[UART] THR: 0x%02x ", c);
                /*if (c == '\n') fprintf(stderr, "\\n");
                else if (c == '\r') fprintf(stderr, "\\r");
                else if (isprint(c)) fprintf(stderr, "'%c'", c);
                else fprintf(stderr, ".");
                fprintf(stderr, "\n");
                */
                // 将字符推送到TX缓冲区（重要！）
                bool success = tx_buf_push(u, b);
                if (!success) {
                    // 缓冲区满，可以设置溢出标志
                    u->lsr |= LSR_OE;
                }


                bool tx_was_empty = tx_buf_is_empty(u);
   
                if(tx_was_empty){
                    pthread_cond_signal(&u->tx_cond);
                }
                u->thr = val & 0xFF;
                u->lsr |= LSR_TX_EMPTY;
                need_irq_update = true;
                /* soc
                // maybe set TX irq pending if buffer empty (depends on semantics)
                if (tx_was_empty && (u->ctrl & UART_CTRL_TX_INT_EN) && u->irq_cb) {
                    u->irq_status |= UART_IRQ_TX_PENDING;
                    u->irq_cb(u->cpu_opaque, 1, u->irq_num);
                    //通知 CPU：
                    //发送通道已空闲，可以写入更多数据到 TX 缓冲区。
                    //这避免了 CPU 轮询 TX 缓冲区状态，提高效率
                } */
            }
            break;
        }
        case 1:
        {
            fflush(stdout);
            if(dlab){
                u->dlm = val;
            }else{
                u->ier = val & 0x0F;
            }

            break;
        }
        case 2: //fcr
        {
            u->fcr = val;
            u->fifo_enable = (u->fcr & 0x01) ? 1:0;
            u->dma_mode = (u->fcr & 0x08) ? 1:0;
            if(u->fcr & 0x02){ //rx fifo reset
                u->rx_count = 0;
                u->rx_head = 0;
                u->rx_tail = 0;
            }else if(u->fcr & 0x04){ // tx fifo reset
                u->tx_count = 0;
                u->tx_head = 0;
                u->rx_tail = 0;
            }
            
            uint8_t trigeer_bits = (u->fcr >> 6) & 0x03;
            switch (trigeer_bits)
            {
            case 0:u->rx_trigger_level = 1; break;
            case 1:u->rx_trigger_level = 4; break;
            case 2:u->rx_trigger_level = 8; break;
            case 3:u->rx_trigger_level = 14; break;
            
            default: break;
            }
            break;
        }
        case 3:
        {
            u->lcr = val;
            break;
        }
        case UART_REG_STATUS:
        {    // status is read-only. ignore writes.
            u->mcr = v & 0x1F;
            break;
        }
        case 5: //lsr
        {
            break;
        }
        case 6: //msr
        {
            break;
        }
        case 7: //scr
        {
            u->scr = val & 0xFF;
        }
        case UART_REG_CTRL:
            u->ctrl = v;
            // control changes may affect IRQ raising immediately
            //uart_maybe_raise_irq(u);
            break;
        case UART_REG_IRQ_STATUS:
            // write-to-clear bits
            u->irq_status &= ~v;
            if (u->irq_status == 0 && u->irq_cb) u->irq_cb(u->cpu_opaque, 0, u->irq_num);
            break;
        case UART_REG_CONFIG:
            // ignore for now
            break;
        default:
            break;
    }
    
    pthread_mutex_unlock(&u->lock);
    fflush(stdout);
    if(offset == 1|| offset == 4){
        uart_update_irq(u);
    }
    if(need_irq_update){
        uart_update_irq(u);
    }
}

/* ---------- lifecycle ---------- */

void uart_init(UARTDevice* uart) {
    memset(uart, 0, sizeof(UARTDevice));
    
    // 初始化寄存器默认值
    uart->lsr = (1 << 5) | (1 << 6); // THRE + TEMT
    uart->msr = 0xB0;                // DCD, DSR, CTS, RI
    /*
    位 7: DCD (Data Carrier Detect) = 1 - 载波检测到 表示“物理链路可用”
    位 5: DSR (Data Set Ready) = 1 - 数据设备就绪 通常设为 1 表示设备正常
    位 4: CTS (Clear To Send) = 1 - 允许发送 表示 TX 通道可用
    位 6: RI (Ring Indicator) = 0 - 无振铃指示 模拟时可设为 0

    */
    uart->mcr = 0x08;                // Loopback mode off
    /*
    0	DTR (Data Terminal Ready)	UART → 外设	表示“终端准备好了”	很少用，一般恒 0
    1	RTS (Request To Send)	UART → 外设	请求发送数据	若启用硬件流控时使用
    2	OUT1	UART → 外设	用户自定义控制位	常未使用
    3	OUT2	UART → 中断逻辑	用于启用中断输出引脚,uart外部输出
    4	LOOP	UART 内部	启用回环模式 (Loopback)	调试时可开启
    5–7	保留	—	—	—
    
    */

    uart->ier = 0x00;                // 中断禁用,内部逻辑
    //IER 是逻辑控制层：选择哪种事件要触发中断；
    //MCR.OUT2 是物理控制层：控制中断信号线是否接通。
    
    uart->iir = 0x01;                // 无中断挂起
    /*
    IIR 告诉 CPU：
        有没有中断挂起；
        挂起的是哪一类（接收数据可用 / THR empty / 线路状态变化）；

    CPU 根据类型读取相应的寄存器（RX / TX / LSR）处理数据。
    
    0	IP (Interrupt Pending)	最低位，1 = 无中断挂起，0 = 有中断挂起
    1	保留	-
    3:2	IID (Interrupt ID)	指示中断类型：
                                00 = 线路状态变化
                                01 = RX 字节可读
                                10 = TX 空
                                11 = Modem 状态变化
    */
    
    // 初始化 FIFO
    uart->rx_head = 0;
    uart->rx_tail = 0;
    uart->rx_count = 0;
    
    // 配置终端为原始模式
    uart->terminal_configured = false;
    if (isatty(STDIN_FILENO)) {//isatty() 检测文件描述符是否连接到终端，确保只在真实终端上配置终端模式。
        struct termios term; //检查标准输入是否是终端设备。

      
        tcgetattr(STDIN_FILENO, &uart->original_termios);
        term = uart->original_termios;
        //    tcgetattr() 获取当前终端的属性
        //保存到 original_termios 以便在程序退出时恢复
        
        cfmakeraw(&term);
        
        /*作用：将终端设置为原始模式。
          原理：cfmakeraw() 将终端配置为：

                禁用回显 (echo)

                禁用规范模式（按行缓冲）

                禁用信号生成（如 Ctrl+C, Ctrl+Z）

                禁用特殊字符处理

                允许逐字符读取
        */

        term.c_oflag |= OPOST;  // 启用输出处理
        //    OPOST 启用输出后处理，允许正常的换行转换等
        //如果没有这个标志，输出可能不会正常换行
                // 只禁用规范模式，保持回显
        term.c_lflag &= ~ICANON;  // 禁用规范模式（逐字符输入）
        // term.c_lflag |= ECHO;   // 确保回显启用（如果需要）
        
        // 设置非阻塞
        term.c_cc[VMIN] = 0;
        term.c_cc[VTIME] = 0;
        
        tcsetattr(STDIN_FILENO, TCSANOW, &term);
        uart->terminal_configured = true;
        /*
            作用：立即应用新的终端设置，并标记终端已配置。
            原理：TCSANOW 表示立即改变终端属性，不等待输出完成。
        */
        //isatty(STDIN_FILENO) 只是 检测环境是否安全，
        //真正让 UART 行为像硬件的是 把终端设置为原始模式。

        
        // 设置非阻塞输入
        fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
        /*
            O_NONBLOCK 使得 read() 调用在没有输入时立即返回而不是阻塞
            这对于模拟器很重要，避免在等待输入时阻塞整个模拟器
        */
    }
}

void uart_cleanup(UARTDevice* uart) {
    if (uart->terminal_configured) {
        tcsetattr(STDIN_FILENO, TCSANOW, &uart->original_termios);
    }
}

void serial_create(UARTDevice *u,const char *serial_dev){
    // 打开串口设备
    u->serial_fd = open(u->serial_device, O_RDWR | O_NOCTTY);
    if (u->serial_fd < 0) {
        perror("Failed to open serial device");
        free(u);
        return NULL;
    }

       // 配置串口
    struct termios options;
    tcgetattr(u->serial_fd, &options);
    
    // 设置波特率
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);
    
    // 8数据位，无校验，1停止位
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    
    // 启用接收，忽略调制解调器状态
    options.c_cflag |= (CLOCAL | CREAD);
    
    // 原始输入模式
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    
    // 原始输出模式  
    options.c_oflag &= ~OPOST;
    
    // 无阻塞读取，立即返回
    options.c_cc[VMIN]  = 0;
    options.c_cc[VTIME] = 0;
    
    tcsetattr(u->serial_fd, TCSANOW, &options);
    
    //strncpy(u->serial_device, serial_dev, sizeof(u->serial_device)-1);
     
}

void uart_update(UARTDevice* uart, uint64_t current_time_ps) {
    // --- 处理发送状态机 ---
    if (uart->tx_in_progress && current_time_ps >= uart->tx_next_bit_time) {
        // 时间到了，发送下一个位

        if (uart->tx_bit_pos == -1) {
            // 刚才发送的是起始位，现在开始发送数据位 LSB
            uart->tx_bit_pos = 0;
            // 设置TX引脚为数据位的第0位 (这里只是模拟，可能没有实际的引脚)
            // simulated_tx_pin = (uart->tx_current_byte >> 0) & 1;
        } else if (uart->tx_bit_pos < 8) {
            // 发送数据位
            uart->tx_bit_pos++;
            // simulated_tx_pin = (uart->tx_current_byte >> uart->tx_bit_pos) & 1;
        } else {
            // 数据位发送完毕，发送停止位
            // simulated_tx_pin = 1; // 停止位为高电平
            // 一帧发送完成！
            uart->tx_in_progress = false;
            // 设置线路状态寄存器：发送保持寄存器空（THRE）和发送器空（TEMT）为1
            uart->lsr |= (1 << 5) | (1 << 6); // 假设第5位是THRE，第6位是TEMT
            // 可以触发一个发送完成中断 here
            return; // 本次更新结束
        }

        // 计算下一个位变化的时间
        uart->tx_next_bit_time += uart->bit_time_ps;
    }
}

UARTDevice *uart_create(uint64_t base_addr, void *cpu_opaque, int irq_num) {
    UARTDevice *u = (UARTDevice *)calloc(1, sizeof(UARTDevice));
    if (!u) return NULL;

    u->baud_rate = 115200;
    // 将位时间转换为皮秒。1秒 = 1e12 皮秒
    u->bit_time_ps = (uint64_t)(1e12 / u->baud_rate);

    u->tx_in_progress = false;

    uart_init(u);
    u->base_addr = base_addr; // cpu access
    u->ctrl = 0;
    u->irq_status = 0;
    pthread_mutex_init(&u->lock, NULL);
    pthread_cond_init(&u->tx_cond, NULL);
    u->tx_head = u->tx_tail = u->tx_count = 0;
    u->rx_head = u->rx_tail = u->rx_count = 0;
    u->cpu_opaque = cpu_opaque;
    //u->irq_cb = irq_cb;
    u->irq_num = irq_num;
    u->running = true;
    u->plic = &plic;
    u->serial_fd = -1;
    u->lsr = (1 << 5) | (1 << 6);
    
    // spawn threads
    if (pthread_create(&u->tx_thread, NULL, uart_tx_thread, u) != 0) {
        perror("uart tx thread create");
        free(u);
        return NULL;
    }
    if (pthread_create(&u->rx_thread, NULL, uart_rx_thread, u) != 0) {
        // rx thread failure is not fatal; continue without rx
        u->running = false;
        pthread_join(u->tx_thread, NULL);
        perror("uart rx thread create");
        free(u);
        return NULL;
    }
    return u;
}

void uart_destroy(UARTDevice *u) {
    if (!u) return;
    pthread_mutex_lock(&u->lock);
    u->running = false;
        // 关闭串口设备
    if (u->serial_fd >= 0) {
        close(u->serial_fd);
        printf("[UART] Serial device closed\n");
    }
    uart_cleanup(u);
    pthread_cond_signal(&u->tx_cond);
    pthread_mutex_unlock(&u->lock);
    pthread_join(u->tx_thread, NULL);
    pthread_join(u->rx_thread, NULL);
    pthread_mutex_destroy(&u->lock);
    pthread_cond_destroy(&u->tx_cond);
    free(u);
}

/* ---------- Example IRQ callback adapter (user provides real CPU IRQ wiring) ---------- */
uart_irq_cb_t cpu_irq_raise_cb(void *opaque, int level, int irq_num) {
    CPU_State *cpu = (CPU_State *)opaque;

    if (level) {
        cpu->irq_pending[irq_num] = 1;  // 拉高中断线
    } else {
        cpu->irq_pending[irq_num] = 0;  // 拉低中断线
    }
}

void uart_update_irq(UARTDevice *u){
    int old_irq = u->irq_pending;
    int new_irq = 0;
    // 检查是否有待处理的中断
    // 检查中断条件
    if ((u->ier & IER_RX_ENABLE) && (u->lsr & LSR_RX_READY)) {
        new_irq = 1;  // 接收中断
    } else if ((u->ier & IER_TX_ENABLE) && (u->lsr & LSR_TX_EMPTY)) {
        new_irq = 1;  // 发送中断
    }
    
    // 如果中断状态发生变化
    if (old_irq != new_irq) {
        u->irq_pending = new_irq;
        
        if (u->plic) {
            plic_set_irq(u->irq_num, new_irq);
        } 
    }
}


uint32_t mmio_read(UARTDevice *uart,uint64_t offset,int size){
    {
        return uart_mmio_read(uart,offset,size);
    }
}

void mmio_write(UARTDevice *uart,uint64_t offset, uint32_t val, int size) {
    
    uint64_t addr = UART_BASE + offset;
    if (addr >= UART_BASE && addr < UART_BASE + UART_SIZE) {
        uart_mmio_write(uart, offset, val, size);
        return;
    }
}

/* ---------- Simple sanity main for local testing ---------- */
#ifdef UART_DEVICE_TEST_MAIN
int main(void) {
    UARTDevice *u = uart_create(0x10000000, NULL, cpu_irq_raise_cb, 3);
    if (!u) { fprintf(stderr, "create failed\n"); return 1; }
    // simple loop to echo: main thread will write 'A' every second
    for (int i = 0; i < 10; ++i) {
        uart_mmio_write(u, UART_REG_DATA, 'A' + i, 4);
        sleep(1);
    }
    uart_destroy(u);
    return 0;
}
#endif


