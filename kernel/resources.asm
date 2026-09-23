section .rodata

; Файлы, вшитые в ядро (иконки, обои, курсор).
; Секция ниже запрещает исполняемый стек для этого объектного модуля
; (иначе линкер помечает стек как исполняемый и ругается).

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


; ==========================================
; Start menu icon (кнопка меню в топбаре)
; ==========================================

global start_icon_bmp_start
global start_icon_bmp_end

start_icon_bmp_start:
    incbin "src/gui/start.bmp"
start_icon_bmp_end:


; ==========================================
; Terminal icon
; ==========================================

global terminal_icon_bmp_start
global terminal_icon_bmp_end

terminal_icon_bmp_start:
    incbin "src/gui/apps/terminal/terminal_icon.bmp"
terminal_icon_bmp_end:


; ==========================================
; Метаданные: запрет исполняемого стека
; ==========================================

section .note.GNU-stack noalloc noexec nowrite progbits