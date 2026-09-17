.section .rodata
.global cursor_bmp_start
.global cursor_bmp_end

cursor_bmp_start:
    .incbin "src/gui/cursor/cursor.bmp"
cursor_bmp_end: