.section .rodata
.global file_icon_bmp_start
.global file_icon_bmp_end

file_icon_bmp_start:
    .incbin "src/gui/apps/file/file_icon.bmp"
file_icon_bmp_end: