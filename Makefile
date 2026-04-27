CROSS = aarch64-linux-gnu-
CC    = $(CROSS)gcc

CFLAGS  = -ffreestanding -nostdlib -O2 -Wall -Wextra
LDFLAGS = -T linker.ld

OBJ = src/boot.o src/kernel.o

all: kernel.elf

kernel.elf: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)

run: kernel.elf
	qemu-system-aarch64 -M virt -cpu cortex-a53 -nographic -kernel kernel.elf

clean:
	rm -f src/*.o kernel.elf