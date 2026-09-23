# ==========================================
# IgorOS Makefile — real Doom (doomgeneric + DOOM.WAD)
# ==========================================

CC = gcc
LD = ld
NASM = nasm

CFLAGS = -m64 -ffreestanding -fno-stack-protector -fno-pie -mno-red-zone \
         -mcmodel=kernel -O2 -Wall -Wextra -Wno-unused -Wno-maybe-uninitialized \
         -Isrc/gui/apps/doom/fs_include \
         -Iinclude -Ikernel -Isrc -Ikernel/include \
         -Isrc/gui/apps/doom -Isrc/gui/apps/doom/engine \
         -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200

LDFLAGS = -m elf_x86_64 -no-pie --no-warn-rwx-segments \
          -T kernel/linker.ld

# ==========================================
# Doom engine (doomgeneric) — всегда в сборке
# ==========================================

DOOM_ENGINE_SRCS = \
    am_map.c d_event.c d_items.c d_iwad.c d_loop.c d_main.c d_mode.c d_net.c \
    doomdef.c doomgeneric.c doomstat.c dstrings.c dummy.c \
    f_finale.c f_wipe.c g_game.c hu_lib.c hu_stuff.c \
    i_cdmus.c i_endoom.c i_input.c i_joystick.c i_scale.c i_sound.c \
    i_system_fs.c i_timer.c i_video.c info.c \
    m_argv.c m_bbox.c m_cheat.c m_config.c m_controls.c m_fixed.c \
    m_menu.c m_misc.c m_random.c memio.c \
    p_ceilng.c p_doors.c p_enemy.c p_floor.c p_inter.c p_lights.c \
    p_map.c p_maputl.c p_mobj.c p_plats.c p_pspr.c p_saveg.c p_setup.c \
    p_sight.c p_spec.c p_switch.c p_telept.c p_tick.c p_user.c \
    r_bsp.c r_data.c r_draw.c r_main.c r_plane.c r_segs.c r_sky.c r_things.c \
    s_sound.c sha1.c sounds.c st_lib.c st_stuff.c statdump.c tables.c \
    v_video.c w_checksum.c w_file.c w_file_stdc.c w_main.c w_wad.c \
    wi_stuff.c z_zone.c

# ==========================================
# Объектные файлы
# ==========================================

OBJS = \
    build/kernel/kernel.o \
    build/kernel/graphics.o \
    build/kernel/idt.o \
    build/kernel/pic.o \
    build/kernel/resources.o \
    build/kernel/isr_stubs.o \
    build/kernel/panic.o \
    build/kernel/timer.o \
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
    build/src/gui/apps/calc/calc_logic.o \
    build/src/gui/apps/calc/calc_app.o \
    build/src/gui/anim/genie_anim.o \
    build/src/gui/anim/win_chrome.o \
    build/src/gui/apps/terminal/terminal_app.o \
    build/src/gui/apps/settings/settings_app.o \
    build/src/gui/apps/doom/doom_app.o \
    build/src/gui/apps/doom/doomgeneric_igoros.o \
    build/src/gui/apps/doom/doom_fs_libc.o \
    build/src/gui/bmp_loader.o \
    build/src/gui/cursor/cursor.o \
    build/src/gui/font.o \
    build/src/graphics.o \
    $(addprefix build/src/gui/apps/doom/engine/,$(DOOM_ENGINE_SRCS:.c=.o))

.PHONY: all clean iso img run run-iso run-img doom-check

all: doom-check iso img

doom-check:
	@test -f src/gui/apps/doom/DOOM.WAD \
		|| (echo "ERROR: нужен файл src/gui/apps/doom/DOOM.WAD" && exit 1)
	@test -f src/gui/apps/doom/doom.bmp \
		|| echo "WARN: нет src/gui/apps/doom/doom.bmp"
	@test -d src/gui/apps/doom/fs_include \
		|| (echo "ERROR: нет src/gui/apps/doom/fs_include/ — распакуй igorOS_doom_fix.zip" && exit 1)
	@test -f src/gui/apps/doom/doom_fs_libc.c \
		|| (echo "ERROR: нет doom_fs_libc.c" && exit 1)
	@test -f src/gui/apps/doom/engine/i_system_fs.c \
		|| (echo "ERROR: нет engine/i_system_fs.c" && exit 1)
	@echo "OK: DOOM.WAD + doom sources + fs_include"

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
	cp build/kernel.elf ready/kernel.elf
	cp boot/limine/limine.conf ready/limine.conf
	cp boot/limine/limine.conf ready/boot/limine/limine.conf
	cp boot/limine/limine.conf ready/EFI/BOOT/limine.conf
	cp boot/limine/limine.cfg  ready/limine.cfg
	cp boot/limine/limine.cfg  ready/boot/limine/limine.cfg
	cp boot/limine/limine.cfg  ready/EFI/BOOT/limine.cfg
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
	dd if=/dev/zero of=$@ bs=1M count=128 status=none
	parted -s $@ mklabel msdos
	parted -s $@ mkpart primary fat32 1MiB 100%
	parted -s $@ set 1 boot on
	mformat -i $@@@1M -F ::
	mmd -i $@@@1M ::/boot
	mmd -i $@@@1M ::/boot/limine
	mmd -i $@@@1M ::/EFI
	mmd -i $@@@1M ::/EFI/BOOT
	mcopy -o -i $@@@1M build/kernel.elf ::/boot/kernel.elf
	mcopy -o -i $@@@1M build/kernel.elf ::/kernel.elf
	mcopy -o -i $@@@1M boot/limine/limine.conf ::/limine.conf
	mcopy -o -i $@@@1M boot/limine/limine.conf ::/boot/limine/limine.conf
	mcopy -o -i $@@@1M boot/limine/limine.conf ::/EFI/BOOT/limine.conf
	mcopy -o -i $@@@1M boot/limine/limine.cfg ::/limine.cfg
	mcopy -o -i $@@@1M boot/limine/limine.cfg ::/boot/limine/limine.cfg
	mcopy -o -i $@@@1M boot/limine/limine.cfg ::/EFI/BOOT/limine.cfg
	mcopy -o -i $@@@1M boot/limine/limine-bios.sys ::/boot/limine/limine-bios.sys
	mcopy -o -i $@@@1M boot/limine/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
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