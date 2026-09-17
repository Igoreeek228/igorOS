section .rodata

; ==========================================
; File Manager icon
; ==========================================

global file_icon_bmp_start
global file_icon_bmp_end

file_icon_bmp_start:
    incbin "src/gui/apps/file/file_icon.bmp"
file_icon_bmp_end:


; ==========================================
; Music icon
; ==========================================

global music_icon_bmp_start
global music_icon_bmp_end

music_icon_bmp_start:
    incbin "src/gui/apps/music/music_icon.bmp"
music_icon_bmp_end:


; ==========================================
; About icon / image
; ==========================================

global about_bmp_start
global about_bmp_end

about_bmp_start:
    incbin "src/gui/apps/about/about.bmp"
about_bmp_end:


; ==========================================
; Wallpaper
; ==========================================

global wallpaper_bmp_start
global wallpaper_bmp_end

wallpaper_bmp_start:
    incbin "src/gui/wallpaper/wallpaper.bmp"
wallpaper_bmp_end:


; ==========================================
; Cursor
; ==========================================

global cursor_bmp_start
global cursor_bmp_end

cursor_bmp_start:
    incbin "src/gui/cursor/cursor.bmp"
cursor_bmp_end:


; ==========================================
; Volume
; ==========================================

global volume_bmp_start
global volume_bmp_end

volume_bmp_start:
    incbin "src/gui/volume.bmp"
volume_bmp_end: