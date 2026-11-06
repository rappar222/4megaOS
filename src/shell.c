#include "shell.h"
#include "vga.h"
#include "keyboard.h"
#include "timer.h"
#include "string.h"
#include "pmm.h"
#include "kheap.h"
#include "io.h"

#define MAX_CMD_LEN 128
#define MAX_ARGS 8

static char line_buf[MAX_CMD_LEN];
static uint32_t line_pos = 0;

// Random number generator using linear congruential generator seeded by PIT ticks
static uint32_t prng_state = 123456789;
static uint32_t rand(void) {
    prng_state = prng_state * 1103515245 + 12345 + timer_get_ticks();
    return (prng_state / 65536) % 32768;
}

static void cmd_help(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprintf("4MegaOS Command Shell - Available Commands:\n");
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    kprintf("  help            - Display this command list\n");
    kprintf("  info            - Display OS specifications and 4MB architecture\n");
    kprintf("  mem             - Display 4MB physical RAM & heap status\n");
    kprintf("  alloc <bytes>   - Allocate <bytes> on kernel heap\n");
    kprintf("  free <hex_addr> - Free heap memory block at given hex address\n");
    kprintf("  testmem         - Stress-test 4MB memory manager & heap\n");
    kprintf("  cpu             - Detect CPU vendor and features via CPUID\n");
    kprintf("  uptime          - Show system uptime and timer ticks\n");
    kprintf("  echo <text>     - Print text to console\n");
    kprintf("  calc <a> <op> <b>- Basic integer math (+, -, *, /)\n");
    kprintf("  color <0-15>    - Change VGA console text color\n");
    kprintf("  clear           - Clear the VGA display\n");
    kprintf("  snake           - Play classic Snake game in 80x25 text mode\n");
    kprintf("  matrix          - Retro Matrix digital rain animation\n");
    kprintf("  reboot          - Reboot the system\n");
    kprintf("  halt            - Halt the CPU\n");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void cmd_info(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    kprintf("==================================================\n");
    kprintf("  4MegaOS - Minimalist 4MB RAM Operating System   \n");
    kprintf("==================================================\n");
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    kprintf("  Target Architecture: x86 32-bit Protected Mode\n");
    kprintf("  Physical Memory    : Exactly 4 MB (4096 KB / 4,194,304 bytes)\n");
    kprintf("  Page Frame Count   : 1024 frames of 4096 bytes\n");
    kprintf("  Frame Bitmap Size  : 128 bytes (tracks 100%% of physical RAM)\n");
    kprintf("  Kernel Load Addr   : 0x100000 (1 MB mark)\n");
    kprintf("  Heap Space         : Dynamic (~2.8 MB available)\n");
    kprintf("  Console            : 80x25 VGA Text Mode + COM1 Serial\n");
    kprintf("  Interrupts         : PIC 8259 + IDT (IRQ 0-15 mapped to 32-47)\n");
    kprintf("  Timer              : 8254 PIT at 100 Hz\n");
    kprintf("  Keyboard           : PS/2 Controller Driver\n");
    kprintf("  Repository         : https://github.com/rappar222/4megaOS\n");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void cmd_mem(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    pmm_print_info();
    kprintf("\n");
    kheap_print_info();
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void cmd_alloc(int argc, char* argv[]) {
    if (argc < 2) {
        kprintf("Usage: alloc <bytes>\n");
        return;
    }
    int bytes = atoi(argv[1]);
    if (bytes <= 0) {
        kprintf("Error: invalid byte count '%s'\n", argv[1]);
        return;
    }

    void* ptr = kmalloc((size_t)bytes);
    if (ptr) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprintf("Successfully allocated %d bytes at %p\n", bytes, ptr);
        // Write pattern to verify memory access
        memset(ptr, 0xAA, (size_t)bytes);
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprintf("Allocation of %d bytes failed: Out of Memory!\n", bytes);
    }
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void cmd_free(int argc, char* argv[]) {
    if (argc < 2) {
        kprintf("Usage: free <hex_addr>\n");
        return;
    }
    uint32_t addr = strtoul_hex(argv[1]);
    if (addr == 0) {
        kprintf("Error: invalid address '%s'\n", argv[1]);
        return;
    }
    kfree((void*)addr);
    kprintf("Freed block at %p\n", (void*)addr);
}

static void cmd_testmem(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_set_color(vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    kprintf("Running 4MB RAM Memory Subsystem Stress Test...\n");

    // 1. Test PMM page allocations
    kprintf("[1/3] Testing PMM frame allocator: ");
    uint32_t f1 = pmm_alloc_frame();
    uint32_t f2 = pmm_alloc_frame();
    uint32_t f3 = pmm_alloc_frame();
    if (f1 && f2 && f3 && f1 != f2 && f2 != f3) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprintf("PASSED (%p, %p, %p)\n", f1, f2, f3);
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprintf("FAILED\n");
    }
    pmm_free_frame(f1);
    pmm_free_frame(f2);
    pmm_free_frame(f3);

    // 2. Test Heap dynamic allocations
    vga_set_color(vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    kprintf("[2/3] Testing Heap multi-block allocation & pattern write: ");
    void* ptrs[8];
    bool passed = true;
    for (int i = 0; i < 8; i++) {
        ptrs[i] = kmalloc(512 * (i + 1));
        if (!ptrs[i]) {
            passed = false;
            break;
        }
        memset(ptrs[i], (uint8_t)(i + 1), 512 * (i + 1));
    }

    if (passed) {
        for (int i = 0; i < 8; i++) {
            uint8_t* byte_ptr = (uint8_t*)ptrs[i];
            for (size_t j = 0; j < 512 * (size_t)(i + 1); j++) {
                if (byte_ptr[j] != (uint8_t)(i + 1)) {
                    passed = false;
                    break;
                }
            }
        }
    }

    if (passed) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprintf("PASSED\n");
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprintf("FAILED\n");
    }

    for (int i = 0; i < 8; i++) {
        if (ptrs[i]) kfree(ptrs[i]);
    }

    // 3. Test Coalescing
    vga_set_color(vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    kprintf("[3/3] Testing Large Block Re-allocation: ");
    void* large = kmalloc(100 * 1024); // 100 KB
    if (large) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprintf("PASSED (allocated 100KB at %p)\n", large);
        kfree(large);
    } else {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprintf("FAILED\n");
    }

    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    kprintf("Memory test complete!\n");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void cmd_cpu(int argc, char* argv[]) {
    (void)argc; (void)argv;
    uint32_t eax, ebx, ecx, edx;

    // CPUID function 0: Vendor String
    asm volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));

    char vendor[13];
    *(uint32_t*)&vendor[0] = ebx;
    *(uint32_t*)&vendor[4] = edx;
    *(uint32_t*)&vendor[8] = ecx;
    vendor[12] = '\0';

    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprintf("CPU Vendor: %s\n", vendor);

    // CPUID function 1: Features
    asm volatile ("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    kprintf("CPU Family: %u | Model: %u | Stepping: %u\n", (eax >> 8) & 0xF, (eax >> 4) & 0xF, eax & 0xF);
    kprintf("Features  : FPU=%s MMX=%s SSE=%s SSE2=%s\n",
            (edx & (1 << 0)) ? "Yes" : "No",
            (edx & (1 << 23)) ? "Yes" : "No",
            (edx & (1 << 25)) ? "Yes" : "No",
            (edx & (1 << 26)) ? "Yes" : "No");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void cmd_uptime(int argc, char* argv[]) {
    (void)argc; (void)argv;
    uint32_t s = timer_get_uptime_seconds();
    uint32_t ticks = timer_get_ticks();
    uint32_t hours = s / 3600;
    uint32_t mins = (s % 3600) / 60;
    uint32_t secs = s % 60;

    kprintf("Uptime: %02u:%02u:%02u (%u PIT ticks)\n", hours, mins, secs, ticks);
}

static void cmd_echo(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        kprintf("%s ", argv[i]);
    }
    kprintf("\n");
}

static void cmd_calc(int argc, char* argv[]) {
    if (argc < 4) {
        kprintf("Usage: calc <num1> <op> <num2> (op: +, -, *, /)\n");
        return;
    }
    int a = atoi(argv[1]);
    char op = argv[2][0];
    int b = atoi(argv[3]);
    int res = 0;

    switch (op) {
        case '+': res = a + b; break;
        case '-': res = a - b; break;
        case '*': res = a * b; break;
        case '/':
            if (b == 0) {
                kprintf("Error: Division by zero!\n");
                return;
            }
            res = a / b;
            break;
        default:
            kprintf("Unknown operator '%c'\n", op);
            return;
    }

    kprintf("%d %c %d = %d\n", a, op, b, res);
}

static void cmd_color(int argc, char* argv[]) {
    if (argc < 2) {
        kprintf("Usage: color <0-15>\n");
        return;
    }
    int col = atoi(argv[1]);
    if (col < 0 || col > 15) {
        kprintf("Color must be between 0 and 15\n");
        return;
    }
    vga_set_color(vga_entry_color((enum vga_color)col, VGA_COLOR_BLACK));
    kprintf("Console color changed to %d.\n", col);
}

static void cmd_clear(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_clear();
}

static void cmd_reboot(int argc, char* argv[]) {
    (void)argc; (void)argv;
    kprintf("Rebooting system...\n");
    timer_sleep(50);

    // Pulse the CPU reset line via 8042 Keyboard Controller
    uint8_t good = 0x02;
    while (good & 0x02) {
        good = inb(0x64);
    }
    outb(0x64, 0xFE);

    // If that fails, trigger triple fault
    asm volatile ("lidt (%0)" : : "r"(0));
    asm volatile ("int3");
}

static void cmd_halt(int argc, char* argv[]) {
    (void)argc; (void)argv;
    kprintf("System halting. You can safely power off.\n");
    // Attempt QEMU / ACPI poweroff ports
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    cli();
    for (;;) {
        hlt();
    }
}

// Matrix Animation
static void cmd_matrix(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_clear();

    int drops[VGA_WIDTH];
    for (int i = 0; i < VGA_WIDTH; i++) {
        drops[i] = -(int)(rand() % VGA_HEIGHT);
    }

    const char* chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%&*";
    size_t char_count = strlen(chars);

    kprintf("Press any key to exit Matrix mode...\n");
    timer_sleep(100);

    while (!keyboard_has_char()) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            if (drops[x] >= 0 && drops[x] < VGA_HEIGHT) {
                char c = chars[rand() % char_count];
                vga_putentry_at(c, vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK), (size_t)x, (size_t)drops[x]);
                if (drops[x] > 0) {
                    // Dim the previous character
                    vga_putentry_at(chars[rand() % char_count], vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK), (size_t)x, (size_t)(drops[x] - 1));
                }
            }

            if (drops[x] - 8 >= 0 && drops[x] - 8 < VGA_HEIGHT) {
                vga_putentry_at(' ', vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_BLACK), (size_t)x, (size_t)(drops[x] - 8));
            }

            drops[x]++;
            if (drops[x] - 8 >= VGA_HEIGHT) {
                drops[x] = -(int)(rand() % 10);
            }
        }
        timer_sleep(5);
    }

    keyboard_getchar(); // Consume key
    vga_clear();
}

// Snake Game in 80x25 text mode
#define SNAKE_W 40
#define SNAKE_H 20
#define SNAKE_MAX_LEN 400

struct Point {
    int x;
    int y;
};

static void cmd_snake(int argc, char* argv[]) {
    (void)argc; (void)argv;
    vga_clear();

    struct Point snake[SNAKE_MAX_LEN];
    int length = 4;
    int dir_x = 1, dir_y = 0;
    int score = 0;

    int offset_x = (VGA_WIDTH - SNAKE_W) / 2;
    int offset_y = (VGA_HEIGHT - SNAKE_H) / 2;

    // Draw borders
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    for (int x = 0; x < SNAKE_W; x++) {
        vga_putentry_at('#', vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK), offset_x + x, offset_y);
        vga_putentry_at('#', vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK), offset_x + x, offset_y + SNAKE_H - 1);
    }
    for (int y = 0; y < SNAKE_H; y++) {
        vga_putentry_at('#', vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK), offset_x, offset_y + y);
        vga_putentry_at('#', vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK), offset_x + SNAKE_W - 1, offset_y + y);
    }

    // Initialize snake in middle
    for (int i = 0; i < length; i++) {
        snake[i].x = offset_x + 10 - i;
        snake[i].y = offset_y + 10;
    }

    // Spawn initial food
    int food_x = offset_x + 1 + (rand() % (SNAKE_W - 2));
    int food_y = offset_y + 1 + (rand() % (SNAKE_H - 2));
    vga_putentry_at('*', vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK), food_x, food_y);

    vga_set_cursor(offset_x, offset_y - 2);
    kprintf("4MegaOS Snake! Controls: W/A/S/D | Q to quit\n");

    bool game_over = false;

    while (!game_over) {
        // Read input if available
        while (keyboard_has_char()) {
            char key = keyboard_getchar();
            if (key == 'w' || key == 'W') { if (dir_y == 0) { dir_x = 0; dir_y = -1; } }
            else if (key == 's' || key == 'S') { if (dir_y == 0) { dir_x = 0; dir_y = 1; } }
            else if (key == 'a' || key == 'A') { if (dir_x == 0) { dir_x = -1; dir_y = 0; } }
            else if (key == 'd' || key == 'D') { if (dir_x == 0) { dir_x = 1; dir_y = 0; } }
            else if (key == 'q' || key == 'Q') { game_over = true; break; }
        }
        if (game_over) break;

        // Calculate new head
        struct Point new_head;
        new_head.x = snake[0].x + dir_x;
        new_head.y = snake[0].y + dir_y;

        // Wall collision check
        if (new_head.x <= offset_x || new_head.x >= offset_x + SNAKE_W - 1 ||
            new_head.y <= offset_y || new_head.y >= offset_y + SNAKE_H - 1) {
            game_over = true;
            break;
        }

        // Self collision check
        for (int i = 0; i < length; i++) {
            if (snake[i].x == new_head.x && snake[i].y == new_head.y) {
                game_over = true;
                break;
            }
        }
        if (game_over) break;

        // Check if food eaten
        bool ate_food = (new_head.x == food_x && new_head.y == food_y);
        if (ate_food) {
            score += 10;
            if (length < SNAKE_MAX_LEN - 1) {
                length++;
            }
            food_x = offset_x + 1 + (rand() % (SNAKE_W - 2));
            food_y = offset_y + 1 + (rand() % (SNAKE_H - 2));
            vga_putentry_at('*', vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK), food_x, food_y);
        } else {
            // Clear tail
            vga_putentry_at(' ', vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK), snake[length - 1].x, snake[length - 1].y);
        }

        // Move body
        for (int i = length - 1; i > 0; i--) {
            snake[i] = snake[i - 1];
        }
        snake[0] = new_head;

        // Draw snake head & body
        vga_putentry_at('O', vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK), snake[0].x, snake[0].y);
        if (length > 1) {
            vga_putentry_at('o', vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK), snake[1].x, snake[1].y);
        }

        // Display Score
        vga_set_cursor(offset_x, offset_y + SNAKE_H);
        kprintf("Score: %d  Length: %d   ", score, length);

        // Adjust speed based on direction (horizontal is twice as fast visually)
        timer_sleep(dir_y != 0 ? 10 : 7);
    }

    vga_set_cursor(offset_x + 8, offset_y + SNAKE_H / 2);
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    kprintf(" GAME OVER! Final Score: %d ", score);
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    timer_sleep(150);
    while (keyboard_has_char()) keyboard_getchar();
    kprintf("\nPress any key to return to shell...");
    keyboard_getchar();
    vga_clear();
}

static struct {
    const char* name;
    void (*func)(int argc, char* argv[]);
} commands[] = {
    {"help",    cmd_help},
    {"?",       cmd_help},
    {"info",    cmd_info},
    {"mem",     cmd_mem},
    {"alloc",   cmd_alloc},
    {"free",    cmd_free},
    {"testmem", cmd_testmem},
    {"cpu",     cmd_cpu},
    {"uptime",  cmd_uptime},
    {"echo",    cmd_echo},
    {"calc",    cmd_calc},
    {"color",   cmd_color},
    {"clear",   cmd_clear},
    {"snake",   cmd_snake},
    {"matrix",  cmd_matrix},
    {"reboot",  cmd_reboot},
    {"halt",    cmd_halt},
    {NULL, NULL}
};

static void execute_command(char* cmd) {
    // Trim leading whitespace
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (*cmd == '\0') return;

    char* argv[MAX_ARGS];
    int argc = 0;

    char* p = cmd;
    while (*p && argc < MAX_ARGS) {
        while (*p == ' ' || *p == '\t') {
            *p++ = '\0';
        }
        if (*p == '\0') break;

        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
    }

    if (argc == 0) return;

    for (int i = 0; commands[i].name != NULL; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].func(argc, argv);
            return;
        }
    }

    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
    kprintf("Unknown command: '%s'. Type 'help' for available commands.\n", argv[0]);
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

void shell_init(void) {
    line_pos = 0;
    line_buf[0] = '\0';
    // Flush any pending characters from keyboard or serial
    while (keyboard_has_char()) {
        keyboard_getchar();
    }
}

static void print_prompt(void) {
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprintf("4megaOS");
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    kprintf("> ");
}

void shell_run(void) {
    print_prompt();

    while (1) {
        char c = keyboard_getchar();

        // Handle Enter
        if (c == '\n' || c == '\r') {
            vga_putchar('\n');
            line_buf[line_pos] = '\0';
            execute_command(line_buf);
            line_pos = 0;
            line_buf[0] = '\0';
            print_prompt();
            continue;
        }

        // Handle Backspace (both ASCII \b and DEL 127)
        if (c == '\b' || c == 127) {
            if (line_pos > 0) {
                line_pos--;
                line_buf[line_pos] = '\0';
                vga_backspace();
            }
            continue;
        }

        // Handle ANSI escape sequences for serial arrow keys
        if (c == 27) {
            if (keyboard_has_char()) {
                char c2 = keyboard_getchar();
                if (c2 == '[') {
                    if (keyboard_has_char()) {
                        keyboard_getchar(); // Consume arrow direction
                    }
                }
            }
            continue;
        }

        // Printable ASCII
        if (c >= 32 && c <= 126) {
            if (line_pos < MAX_CMD_LEN - 1) {
                line_buf[line_pos++] = c;
                line_buf[line_pos] = '\0';
                vga_putchar(c);
            }
        }
    }
}
