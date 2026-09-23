# ==========================================
# IgorOS Makefile
# ==========================================

CC = gcc
LD = ld
NASM = nasm

# -fno-builtin: в freestanding-среде нет библиотечных реализаций,
#   встроенные функции компилятора могут порождать вызовы в несуществующую libc.
# -z noexecstack: стек не должен быть исполняемым (предупреждение линкера).
CFLAGS = -m64 -std=gnu11 -ffreestanding -fno-builtin -fno-stack-protector \
         -fno-pie -mno-red-zone -mcmodel=kernel -O2 -Wall -Wextra \
         -z noexecstack \
         -Ikernel -Isrc -Ikernel/include

LDFLAGS = -m elf_x86_64 -no-pie -z noexecstack --no-warn-rwx-segments \
          -T kernel/linker.ld

# ==========================================
# Объектные файлы
# ==========================================

OBJS = \
    build/kernel/kernel.o \
    build/kernel/idt.o \
    build/kernel/pic.o \
    build/kernel/resources.o \
    build/boot/loading/load_logo.o \
    build/src/desktop.o \
    build/src/drivers/system/keyboard.o \
    build/src/drivers/system/mouse.o \
    build/src/drivers/system/ac97.o \
    build/src/drivers/system/hda.o \
    build/src/drivers/system/sound_manager.o \
    build/src/drivers/system/ata.o \
    build/src/drivers/system/fat32.o \
    build/src/gui/apps/file/file_manager.o \
    build/src/gui/apps/music/music_app.o \
    build/src/gui/apps/about/about_app.o \
    build/src/gui/apps/terminal/terminal_app.o \
    build/src/gui/bmp_loader.o \
    build/src/gui/cursor/cursor.o \
    build/src/gui/font.o

.PHONY: all clean iso img run run-iso run-img

all: iso img

# ==========================================
# Компиляция kernel/include/*.c
# ==========================================

build/kernel/%.o: kernel/include/%.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# ==========================================
# Компиляция обычных C-файлов
# ==========================================

build/%.o: %.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# ==========================================
# ASM
# ==========================================

build/%.o: %.asm
	mkdir -p $(@D)
	$(NASM) -f elf64 $< -o $@

# ==========================================
# Ядро
# ==========================================

build/kernel.elf: $(OBJS)
	mkdir -p $(@D)
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# ==========================================
# ISO
# ==========================================

iso: build/igorOS.iso

build/igorOS.iso: build/kernel.elf boot/limine/limine.conf boot/limine/limine.cfg
	@echo "==> Создание структуры ISO..."

	rm -rf ready

	mkdir -p ready/boot/limine
	mkdir -p ready/EFI/BOOT

	cp build/kernel.elf ready/boot/kernel.elf

	# --- limine.conf (современный) ---
	cp boot/limine/limine.conf ready/limine.conf
	cp boot/limine/limine.conf ready/boot/limine/limine.conf
	cp boot/limine/limine.conf ready/EFI/BOOT/limine.conf

	# --- limine.cfg (для совместимости BIOS / старых версий) ---
	cp boot/limine/limine.cfg ready/limine.cfg
	cp boot/limine/limine.cfg ready/boot/limine/limine.cfg
	cp boot/limine/limine.cfg ready/EFI/BOOT/limine.cfg

	cp boot/limine/limine-bios-cd.bin   ready/boot/limine/
	cp boot/limine/limine-uefi-cd.bin   ready/boot/limine/
	cp boot/limine/limine-bios.sys      ready/boot/limine/

	-cp boot/limine/BOOTX64.EFI ready/EFI/BOOT/ 2>/dev/null || true

	xorriso -as mkisofs \
		-b boot/limine/limine-bios-cd.bin \
		-no-emul-boot \
		-boot-load-size 4 \
		-boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part \
		--efi-boot-image \
		--eltorito-alt-boot \
		ready \
		-o build/igorOS.iso

	./boot/limine/limine bios-install build/igorOS.iso

# ==========================================
# IMG
# ==========================================

img: build/igorOS.img

build/igorOS.img: build/kernel.elf boot/limine/limine.conf boot/limine/limine.cfg boot/limine/BOOTX64.EFI
	@echo "==> Создание IMG..."

	dd if=/dev/zero of=$@ bs=1M count=64 status=none

	parted -s $@ mklabel msdos
	parted -s $@ mkpart primary fat32 1MiB 100%
	parted -s $@ set 1 boot on

	mformat -i $@@@1M -F ::

	mmd -i $@@@1M ::/boot
	mmd -i $@@@1M ::/boot/limine
	mmd -i $@@@1M ::/EFI
	mmd -i $@@@1M ::/EFI/BOOT

	mcopy -o -i $@@@1M \
		build/kernel.elf \
		::/boot/kernel.elf

	# --- limine.conf ---
	mcopy -o -i $@@@1M boot/limine/limine.conf ::/limine.conf
	mcopy -o -i $@@@1M boot/limine/limine.conf ::/boot/limine/limine.conf
	mcopy -o -i $@@@1M boot/limine/limine.conf ::/EFI/BOOT/limine.conf

	# --- limine.cfg (совместимость) ---
	mcopy -o -i $@@@1M boot/limine/limine.cfg ::/limine.cfg
	mcopy -o -i $@@@1M boot/limine/limine.cfg ::/boot/limine/limine.cfg
	mcopy -o -i $@@@1M boot/limine/limine.cfg ::/EFI/BOOT/limine.cfg

	mcopy -o -i $@@@1M \
		boot/limine/limine-bios.sys \
		::/boot/limine/limine-bios.sys

	mcopy -o -i $@@@1M \
		boot/limine/BOOTX64.EFI \
		::/EFI/BOOT/BOOTX64.EFI

	@echo "==> Проверка UEFI загрузчика..."
	mdir -i $@@@1M ::/EFI/BOOT
	@echo "==> Установка BIOS Limine..."
	./boot/limine/limine bios-install $@

# ==========================================
# QEMU
# ==========================================

run: run-iso

run-iso: build/igorOS.iso
	qemu-system-x86_64 \
		-cdrom build/igorOS.iso \
		-m 2048 \
		-vga std \
		-serial stdio \
		-no-shutdown \
		-no-reboot

run-img: build/igorOS.img
	qemu-system-x86_64 \
		-hda build/igorOS.img \
		-m 2048 \
		-vga std \
		-serial stdio \
		-no-shutdown \
		-no-reboot

# ==========================================
# CLEAN
# ==========================================

clean:
	rm -rf build ready