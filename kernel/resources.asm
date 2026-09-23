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


; ==========================================
; Calculator icon (IgorOS Nord — портировано как фича из OriginOS,
; иконка в стиле IgorOS/Big Sur)
; ==========================================

global calc_icon_bmp_start
global calc_icon_bmp_end

calc_icon_bmp_start:
    incbin "src/gui/apps/calc/calc_icon.bmp"
calc_icon_bmp_end:


; ==========================================
; Notes icon (IgorOS Nord)
; ==========================================

global notes_icon_bmp_start
global notes_icon_bmp_end

notes_icon_bmp_start:
    incbin "src/gui/apps/notes/notes_icon.bmp"
notes_icon_bmp_end:


; ==========================================
; Settings icon (IgorOS Nord)
; ==========================================

global settings_icon_bmp_start
global settings_icon_bmp_end

settings_icon_bmp_start:
    incbin "src/gui/apps/settings/settings_icon.bmp"
settings_icon_bmp_end:


; ==========================================
; Terminal icon (IgorOS Nord)
; ==========================================

global terminal_icon_bmp_start
global terminal_icon_bmp_end

terminal_icon_bmp_start:
    incbin "src/gui/apps/file/terminal_icon.bmp"
terminal_icon_bmp_end:


; ==========================================
; Wallpapers for Settings (Day / Night) — same folder as wallpaper.bmp
; ==========================================

global wallpaper_day_bmp_start
global wallpaper_day_bmp_end

wallpaper_day_bmp_start:
    incbin "src/gui/wallpaper/wallpaper_day.bmp"
wallpaper_day_bmp_end:

global wallpaper_night_bmp_start
global wallpaper_night_bmp_end

wallpaper_night_bmp_start:
    incbin "src/gui/wallpaper/wallpaper_night.bmp"
wallpaper_night_bmp_end:

; ==========================================
; DOOM dock icon
; ==========================================

global doom_icon_bmp_start
global doom_icon_bmp_end

doom_icon_bmp_start:
    incbin "src/gui/apps/doom/doom.bmp"
doom_icon_bmp_end:


; ==========================================
; DOOM IWAD (place file at src/gui/apps/doom/DOOM.WAD)
; ==========================================

global doom_wad_start
global doom_wad_end

doom_wad_start:
    incbin "src/gui/apps/doom/DOOM.WAD"
doom_wad_end:
