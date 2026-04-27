#include <stdint.h>

/* =========================
   UART (QEMU virt PL011)
   ========================= */

#define UART0_BASE 0x09000000UL

#define UART_DR   (*(volatile unsigned int*)(UART0_BASE + 0x000))
#define UART_FR   (*(volatile unsigned int*)(UART0_BASE + 0x018))
#define UART_IBRD (*(volatile unsigned int*)(UART0_BASE + 0x024))
#define UART_FBRD (*(volatile unsigned int*)(UART0_BASE + 0x028))
#define UART_LCR  (*(volatile unsigned int*)(UART0_BASE + 0x02C))
#define UART_CR   (*(volatile unsigned int*)(UART0_BASE + 0x030))

#define UART_FR_RXFE  (1 << 4)
#define UART_FR_TXFF  (1 << 5)
#define UART_FR_BUSY  (1 << 3)

#define UART_LCR_FEN   (1 << 4)
#define UART_LCR_WLEN8 (3 << 5)

void uart_init(void) {
    UART_CR = 0;
    while (UART_FR & UART_FR_BUSY);
    UART_IBRD = 26;
    UART_FBRD = 3;
    UART_LCR = UART_LCR_WLEN8 | UART_LCR_FEN;
    UART_CR = (1 << 0) | (1 << 8) | (1 << 9);
}

void uart_putc(char c) {
    while (UART_FR & UART_FR_TXFF);
    UART_DR = (unsigned int)(unsigned char)c;
}

char uart_getc(void) {
    while (UART_FR & UART_FR_RXFE);
    return (char)(UART_DR & 0xFF);
}

int uart_has_data(void) {
    return !(UART_FR & UART_FR_RXFE);
}

void uart_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

/* =========================
   String utils
   ========================= */

int k_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

void k_strncpy(char *dest, const char *src, int n) {
    int i;
    for (i = 0; i < n - 1 && src[i] != '\0'; i++) dest[i] = src[i];
    dest[i] = '\0';
}

int k_strlen(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

int atoi_simple(const char *s) {
    if (!s) return 0;
    int result = 0, neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { result = result * 10 + (*s - '0'); s++; }
    return neg ? -result : result;
}

void itoa_simple(int v, char *buf) {
    char tmp[16];
    int i = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    while (v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; }
    int start = 0;
    if (neg) buf[start++] = '-';
    for (int j = 0; j < i; j++) buf[start + j] = tmp[i - j - 1];
    buf[start + i] = '\0';
}

/* =========================
   Key & Input Parsing
   ========================= */

#define KEY_UP        1000
#define KEY_DOWN      1001
#define KEY_RIGHT     1002
#define KEY_LEFT      1003
#define KEY_ESC       27
#define KEY_ENTER     13
#define KEY_BACKSPACE 127

int read_key(void) {
    char c = uart_getc();
    if (c == '\r' || c == '\n') return KEY_ENTER;
    if (c == '\b' || c == 127) return KEY_BACKSPACE;
    
    if (c == 27) {
        // If an escape sequence is arriving, the next chars should be immediate
        if (!uart_has_data()) return KEY_ESC;
        char seq1 = uart_getc();
        if (seq1 == '[') {
            char seq2 = uart_getc();
            if (seq2 == 'A') return KEY_UP;
            if (seq2 == 'B') return KEY_DOWN;
            if (seq2 == 'C') return KEY_RIGHT;
            if (seq2 == 'D') return KEY_LEFT;
        }
        return KEY_ESC;
    }
    return c;
}

void read_line(char *buf, int max) {
    int i = 0;
    while (1) {
        int k = read_key();
        if (k == KEY_ENTER) { uart_puts("\r\n"); buf[i] = '\0'; return; }
        if (k == KEY_BACKSPACE) { if (i > 0) { i--; uart_puts("\b \b"); } continue; }
        if (k < 0x20 || k > 0x7E) continue;
        if (i >= max - 1) { uart_putc('\a'); continue; }
        buf[i++] = (char)k;
        uart_putc((char)k);
    }
}

void parse(char *input, char **cmd, char **args, int max_args) {
    int arg_count = 0;
    for (int j = 0; j < max_args; j++) args[j] = (char *)0;
    while (*input == ' ') input++;
    if (*input == '\0') { *cmd = input; return; }
    *cmd = input;
    while (*input && *input != ' ') input++;
    while (*input) {
        if (*input == ' ') {
            *input = '\0';
            input++;
            while (*input == ' ') input++;
            if (*input != '\0' && arg_count < max_args) {
                args[arg_count++] = input;
                while (*input && *input != ' ') input++;
            }
        } else input++;
    }
}

/* =========================
   In-Memory File System
   ========================= */

#define MAX_FILES 8
#define MAX_FILENAME 16
#define MAX_FILE_SIZE 2048

typedef struct {
    int in_use;
    char name[MAX_FILENAME];
    char data[MAX_FILE_SIZE];
    int size;
} file_t;

file_t ramfs[MAX_FILES];

void fs_init(void) {
    for (int i = 0; i < MAX_FILES; i++) ramfs[i].in_use = 0;
}

int fs_find(const char *name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (ramfs[i].in_use && k_strcmp(ramfs[i].name, name) == 0) return i;
    }
    return -1;
}

/* =========================
   Tix command macro
   ========================= */

#define TIX_COMMAND(name)       void cmd_##name(char **tixArg)
#define TIX_REGISTER(str, name) { str, cmd_##name }

void clrscrn(void) { uart_puts("\033[1;1H\033[2J"); }

/* =========================
   AS Editor (Neovim Inspired)
   ========================= */

#define EDIT_ROWS 20
#define EDIT_COLS 78

TIX_COMMAND(as) {
    const char *filename = tixArg[0];
    char name_buf[MAX_FILENAME];

    /* Interactive Menu */
    if (!filename) {
        int selected = 0;
        while (1) {
            clrscrn();
            uart_puts("\r\n\r\n                    === AS EDITOR ===\r\n");
            uart_puts("                 The ASCII Editor for Tix\r\n\r\n");

            if (selected == 0) uart_puts("\033[7m");
            uart_puts("                 [N] New File                    \r\n");
            if (selected == 0) uart_puts("\033[0m");

            if (selected == 1) uart_puts("\033[7m");
            uart_puts("                 [O] Open File                   \r\n");
            if (selected == 1) uart_puts("\033[0m");

            if (selected == 2) uart_puts("\033[7m");
            uart_puts("                 [Q] Quit                        \r\n");
            if (selected == 2) uart_puts("\033[0m");

            int k = read_key();
            if (k == KEY_UP && selected > 0) selected--;
            if (k == KEY_DOWN && selected < 2) selected++;
            if (k == KEY_ENTER) {
                if (selected == 2) { clrscrn(); return; }
                if (selected == 0) {
                    uart_puts("\r\n                 Enter new filename: ");
                    read_line(name_buf, sizeof(name_buf));
                    filename = name_buf;
                    break;
                }
                if (selected == 1) {
                    uart_puts("\r\n                 Enter filename to open: ");
                    read_line(name_buf, sizeof(name_buf));
                    filename = name_buf;
                    break;
                }
            }
        }
    }

    if (!filename || filename[0] == '\0') return;

    int idx = fs_find(filename);
    if (idx == -1) {
        for (int i = 0; i < MAX_FILES; i++) {
            if (!ramfs[i].in_use) {
                ramfs[i].in_use = 1;
                k_strncpy(ramfs[i].name, filename, MAX_FILENAME);
                ramfs[i].data[0] = '\0';
                ramfs[i].size = 0;
                idx = i;
                break;
            }
        }
        if (idx == -1) { uart_puts("error: file system full.\r\n"); return; }
    }

    /* Editor State */
    char text[EDIT_ROWS][EDIT_COLS];
    for (int r = 0; r < EDIT_ROWS; r++) for (int c = 0; c < EDIT_COLS; c++) text[r][c] = 0;

    int r = 0, c = 0;
    for (int i = 0; i < ramfs[idx].size; i++) {
        char ch = ramfs[idx].data[i];
        if (ch == '\r') continue;
        if (ch == '\n') { r++; c = 0; continue; }
        if (r < EDIT_ROWS && c < EDIT_COLS) text[r][c++] = ch;
    }
    int max_r = (r < EDIT_ROWS) ? r : EDIT_ROWS - 1;
    
    int cx = 0, cy = 0;
    int mode = 0; // 0=NORMAL, 1=INSERT, 2=COMMAND
    char cmd_buf[32] = {0};
    int cmd_len = 0;

    while (1) {
        /* Render Screen */
        uart_puts("\033[H"); 
        for (int i = 0; i < EDIT_ROWS; i++) {
            for (int j = 0; j < EDIT_COLS; j++) uart_putc(text[i][j] ? text[i][j] : ' ');
            uart_puts("\r\n");
        }

        /* Status Line */
        uart_puts("\033[7m"); 
        if (mode == 0) uart_puts("-- NORMAL --                                                                  ");
        else if (mode == 1) uart_puts("-- INSERT --                                                                  ");
        else {
            uart_puts(":"); uart_puts(cmd_buf);
            for(int i = k_strlen(cmd_buf) + 1; i < 78; i++) uart_putc(' ');
        }
        uart_puts("\033[0m\r\n");

        /* Place Cursor */
        if (mode == 2) {
            uart_puts("\033[21;"); // Bottom line
            char num[16]; itoa_simple(cmd_len + 2, num); uart_puts(num); uart_puts("H");
        } else {
            uart_puts("\033[");
            char num[16]; itoa_simple(cy + 1, num); uart_puts(num);
            uart_puts(";");
            itoa_simple(cx + 1, num); uart_puts(num);
            uart_puts("H");
        }

        /* Input Handling */
        int k = read_key();

        if (mode == 0) {
            if (k == 'i' || k == 'I') mode = 1;
            else if (k == ':') { mode = 2; cmd_len = 0; cmd_buf[0] = 0; }
            else if (k == KEY_UP && cy > 0) cy--;
            else if (k == KEY_DOWN && cy < max_r) cy++;
            else if (k == KEY_LEFT && cx > 0) cx--;
            else if (k == KEY_RIGHT && cx < EDIT_COLS - 1) cx++;
        } else if (mode == 1) {
            if (k == KEY_ESC) mode = 0;
            else if (k == KEY_UP && cy > 0) cy--;
            else if (k == KEY_DOWN && cy < max_r) cy++;
            else if (k == KEY_LEFT && cx > 0) cx--;
            else if (k == KEY_RIGHT && cx < EDIT_COLS - 1) cx++;
            else if (k == KEY_BACKSPACE) {
                if (cx > 0) {
                    cx--;
                    for (int j = cx; j < EDIT_COLS - 1; j++) text[cy][j] = text[cy][j+1];
                    text[cy][EDIT_COLS-1] = 0;
                }
            } else if (k == KEY_ENTER) {
                if (max_r < EDIT_ROWS - 1) {
                    for (int i = max_r; i >= cy; i--) {
                        for (int j = 0; j < EDIT_COLS; j++) text[i+1][j] = text[i][j];
                    }
                    for (int j = cx; j < EDIT_COLS; j++) { text[cy+1][j-cx] = text[cy][j]; text[cy][j] = 0; }
                    max_r++; cy++; cx = 0;
                }
            } else if (k >= 0x20 && k <= 0x7E) {
                for (int j = EDIT_COLS - 1; j > cx; j--) text[cy][j] = text[cy][j-1];
                text[cy][cx] = k;
                if (cx < EDIT_COLS - 1) cx++;
            }
        } else if (mode == 2) {
            if (k == KEY_ESC) mode = 0;
            else if (k == KEY_BACKSPACE && cmd_len > 0) cmd_buf[--cmd_len] = 0;
            else if (k == KEY_ENTER) {
                if (k_strcmp(cmd_buf, "w") == 0 || k_strcmp(cmd_buf, "wq") == 0) {
                    int pos = 0;
                    for (int i = 0; i <= max_r; i++) {
                        int end = EDIT_COLS - 1;
                        while (end >= 0 && text[i][end] == 0) end--;
                        for (int j = 0; j <= end; j++) {
                            if (pos < MAX_FILE_SIZE - 2) ramfs[idx].data[pos++] = text[i][j] ? text[i][j] : ' ';
                        }
                        if (i < max_r && pos < MAX_FILE_SIZE - 2) {
                            ramfs[idx].data[pos++] = '\r';
                            ramfs[idx].data[pos++] = '\n';
                        }
                    }
                    ramfs[idx].data[pos] = '\0';
                    ramfs[idx].size = pos;
                }
                if (k_strcmp(cmd_buf, "q") == 0 || k_strcmp(cmd_buf, "wq") == 0 || k_strcmp(cmd_buf, "q!") == 0) {
                    clrscrn(); return;
                }
                mode = 0; 
            } else if (k >= 0x20 && k <= 0x7E && cmd_len < 30) {
                cmd_buf[cmd_len++] = k;
                cmd_buf[cmd_len] = 0;
            }
        }
    }
}

/* =========================
   Standard Commands
   ========================= */

TIX_COMMAND(echo) { if (tixArg[0]) uart_puts(tixArg[0]); uart_puts("\r\n"); }
TIX_COMMAND(clearscrn) { clrscrn(); }

TIX_COMMAND(fread) {
    const char *filename = tixArg[0];
    if (!filename) { uart_puts("usage: fread <filename>\r\n"); return; }
    int idx = fs_find(filename);
    if (idx == -1) { uart_puts("error: file not found\r\n"); return; }
    if (ramfs[idx].size == 0) uart_puts("<empty file>\r\n");
    else { uart_puts(ramfs[idx].data); uart_puts("\r\n"); }
}

TIX_COMMAND(help) {
    (void)tixArg;
    uart_puts("commands:\r\n");
    uart_puts("  echo <text>       print text\r\n");
    uart_puts("  fread <name>      read contents of a file\r\n");
    uart_puts("  as [name]         open file in AS editor (or open menu)\r\n");
    uart_puts("  clear             clear screen\r\n");
    uart_puts("  help              show this list\r\n");
}

/* =========================
   Dispatch table & Kernel Loop
   ========================= */

typedef void (*cmd_fn)(char **args);
typedef struct { const char *name; cmd_fn fn; } command_t;

static const command_t commands[] = {
    TIX_REGISTER("echo",  echo),
    TIX_REGISTER("fread", fread),
    TIX_REGISTER("as",    as),
    TIX_REGISTER("help",  help),
    TIX_REGISTER("clear", clearscrn),
    { (char *)0, (cmd_fn)0 },
};

void process_command(char *input) {
    char *cmd = (char *)0;
    char *args[4];
    parse(input, &cmd, args, 4);
    if (!cmd || *cmd == '\0') return;
    for (int i = 0; commands[i].name != (char *)0; i++) {
        if (k_strcmp(cmd, commands[i].name) == 0) {
            commands[i].fn(args);
            return;
        }
    }
    uart_puts("unknown: "); uart_puts(cmd); uart_puts("\r\ntype 'help'\r\n");
}

void tix_init(void) {
    char buf[64];
    while (1) {
        uart_puts(": ");
        read_line(buf, sizeof(buf));
        process_command(buf);
    }
}

void kernel_main(void) {
    uart_init();
    fs_init();
    clrscrn();
    uart_puts(
        "\t      _____    ___  __      __ ___   __  __  \r\n"
        "\t     |_   _|  |_ _| \\ \\    / /|_ _|  \\ \\/ /  \r\n"
        "\t       | |     | |   \\ \\/\\/ /  | |    >  <   \r\n"
        "\t      _|_|_   |___|   \\_/\\_/  |___|  /_/\\_\\  \r\n"
        "\t    _|\"\"\"\"\"|_|\"\"\"\"\"|_|\"\"\"\"\"|_|\"\"\"\"\"|_|\"\"\"\"\"| \r\n"
        "\t    \"`-0-0-'\"`-0-0-'\"`-0-0-'\"`-0-0-'\"`-0-0-' \r\n"
    );
    uart_puts("    This operating system was written in C and AArch64 Assembly.\n    Assembly as stem and C as branch.\n\t\tLicensed under the GNU general public lisence V3.0\n\n    The pointless operating system written for the fun we need! Expect bugs we guess.\n\nBegin typin'!\n");
    tix_init();
    while (1) {}
}