.global wallpaper_bmp_start
.global wallpaper_bmp_end

.section .rodata
wallpaper_bmp_start:
    .incbin "src/gui/wallpaper/wallpaper.bmp"
wallpaper_bmp_end: