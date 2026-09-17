// src/gui/apps/file/file_manager.c

#include "gui/apps/file/file_manager.h"
#include "gui/font.h"
#include "drivers/system/fat32.h"

extern void draw_rounded_rect_buf(int x, int y, int w, int h, int r, uint32_t color);
extern void draw_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
extern void draw_rect_buf(int x, int y, int w, int h, uint32_t color);
extern void draw_pixel_buf(int x, int y, uint32_t color);

// Рисует прямоугольник, скруглённый ТОЛЬКО у одного нижнего угла (левого или
// правого) — сверху и у второго нижнего угла остаются прямыми. Нужно для
// боковой панели/инспектора: они примыкают к прямому краю шапки сверху, но
// снизу должны повторять скругление внешнего угла окна, а не резать его.
static void draw_panel_rounded_bottom(int x, int y, int w, int h, int r, uint32_t color, int round_left, int round_right) {
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            int rx = -1, ry = -1;
            if (round_left && j < r && i >= h - r) { rx = r - j - 1; ry = i - (h - r); }
            else if (round_right && j >= w - r && i >= h - r) { rx = j - (w - r); ry = i - (h - r); }

            if (rx != -1 && ry != -1) {
                if (rx * rx + ry * ry <= r * r) draw_pixel_buf(x + j, y + i, color);
            } else {
                draw_pixel_buf(x + j, y + i, color);
            }
        }
    }
}

static int is_open = 0;
static int win_x = 140, win_y = 80;
static int win_w = 760, win_h = 460;
static int dragging = 0, drag_ox = 0, drag_oy = 0;

static vfs_node_t root_dir;
static vfs_node_t docs_dir;
static vfs_node_t media_dir;
static vfs_node_t sample_txt;
static vfs_node_t* current_dir = &root_dir;
static int selected_idx = -1;

// --- Узлы реального FAT32-диска (см. drivers/fat32.c) ---
static vfs_node_t disk_root;
static int disk_available = 0;
static vfs_node_t disk_pool[64];
static int disk_pool_used = 0;

static uint8_t real_file_buf[16384];
static vfs_node_t* last_loaded_file = (void*)0;

static vfs_node_t* alloc_disk_node(void) {
    if (disk_pool_used >= 64) return 0;
    return &disk_pool[disk_pool_used++];
}

// Читает содержимое директории с реального диска в её children (один раз —
// повторный вызов для уже populated-директории ничего не делает).
static void populate_dir_from_disk(vfs_node_t* dir) {
    if (dir->populated) return;
    dir->populated = 1;
    dir->child_count = 0;

    fat32_entry_t entries[16];
    int count = fat32_list_dir(dir->fat_cluster, entries, 16);

    for (int i = 0; i < count && dir->child_count < 16; i++) {
        vfs_node_t* node = alloc_disk_node();
        if (!node) break;

        int j = 0;
        for (; entries[i].name[j]; j++) node->name[j] = entries[i].name[j];
        node->name[j] = 0;

        node->type = entries[i].is_dir ? NODE_DIR : NODE_FILE;
        node->size = entries[i].size;
        node->data = (void*)0;
        node->parent = dir;
        node->child_count = 0;
        node->fat_cluster = entries[i].first_cluster;
        node->is_real_disk = 1;
        node->populated = 0;

        dir->children[dir->child_count++] = node;
    }
}

static const char text_data[] = "Hello from igorOS!\nRAMFS file reader working.";

static void format_size(uint32_t bytes, char* out) {
    if (bytes < 1024) {
        out[0] = '0' + (bytes / 100) % 10;
        out[1] = '0' + (bytes / 10) % 10;
        out[2] = '0' + (bytes % 10);
        out[3] = ' '; out[4] = 'B'; out[5] = 0;
    } else {
        uint32_t kb = bytes / 1024;
        out[0] = '0' + (kb / 100) % 10;
        out[1] = '0' + (kb / 10) % 10;
        out[2] = '0' + (kb % 10);
        out[3] = ' '; out[4] = 'K'; out[5] = 'B'; out[6] = 0;
    }
}

void init_file_manager(void) {
    root_dir.type = NODE_DIR;
    root_dir.parent = &root_dir;
    root_dir.child_count = 0;
    root_dir.name[0] = '/'; root_dir.name[1] = 0;

    docs_dir.type = NODE_DIR;
    docs_dir.parent = &root_dir;
    docs_dir.child_count = 0;
    const char* dname = "Documents";
    for(int i=0; dname[i]; i++) docs_dir.name[i] = dname[i];

    media_dir.type = NODE_DIR;
    media_dir.parent = &root_dir;
    media_dir.child_count = 0;
    const char* mname = "Media";
    for(int i=0; mname[i]; i++) media_dir.name[i] = mname[i];

    sample_txt.type = NODE_FILE;
    sample_txt.size = sizeof(text_data);
    sample_txt.data = (const uint8_t*)text_data;
    sample_txt.parent = &docs_dir;
    const char* tname = "readme.txt";
    for(int i=0; tname[i]; i++) sample_txt.name[i] = tname[i];

    docs_dir.children[0] = &sample_txt;
    docs_dir.child_count = 1;

    root_dir.children[0] = &docs_dir;
    root_dir.children[1] = &media_dir;
    root_dir.child_count = 2;

    // Пробуем найти настоящий FAT32-раздел на диске (см. drivers/fat32.c).
    // Если диска нет (например, запуск через -cdrom, а не -hda) — просто
    // не показываем "Local Disk", без диска ему взяться неоткуда.
    disk_available = fat32_init();
    if (disk_available) {
        disk_root.type = NODE_DIR;
        disk_root.parent = &root_dir;
        disk_root.child_count = 0;
        disk_root.fat_cluster = fat32_root_cluster();
        disk_root.is_real_disk = 1;
        disk_root.populated = 0;
        const char* dname = "Local Disk";
        int i = 0;
        for (; dname[i]; i++) disk_root.name[i] = dname[i];
        disk_root.name[i] = 0;

        populate_dir_from_disk(&disk_root); // корень читаем сразу, это дёшево

        root_dir.children[root_dir.child_count++] = &disk_root;
    }
}

void toggle_file_manager(void) {
    if (!root_dir.name[0]) init_file_manager();
    is_open = !is_open;
}

void render_file_manager_window(uint32_t* buf, int scr_w, int scr_h, int mx, int my, int btn, int click) {
    (void)scr_h;
    if (!is_open) return;

    // Зона перетаскивания окна за шапку (исключая область светофора справа)
    int traffic_zone_w = 90; // светофор + отступы, сюда драг не должен реагировать
    if (btn && !dragging && mx >= win_x && mx <= win_x + win_w - traffic_zone_w && my >= win_y && my <= win_y + 36) {
        dragging = 1; drag_ox = mx - win_x; drag_oy = my - win_y;
    }
    if (!btn) dragging = 0;
    if (dragging) { win_x = mx - drag_ox; win_y = my - drag_oy; }

    uint32_t BG_MAIN = 0x00F6F6F8;
    uint32_t BG_SIDEBAR = 0x00EDEDF0;
    uint32_t BG_INSPECTOR = 0x00F0F0F3;
    uint32_t COLOR_TEXT = 0x001C1C1E;

    // --- Мягкая тень (несколько полупрозрачных слоёв вместо одного резкого) ---
    // Идём от самого широкого/прозрачного слоя к самому узкому/плотному —
    // так получается градиент размытия без реального блюра.
    static const struct { int spread; int drop; uint8_t alpha; } shadow_layers[] = {
        { 10, 14, 10 },
        { 8,  12, 14 },
        { 6,  10, 18 },
        { 4,   8, 24 },
        { 2,   6, 32 },
    };
    for (unsigned i = 0; i < sizeof(shadow_layers)/sizeof(shadow_layers[0]); i++) {
        int sp = shadow_layers[i].spread;
        draw_rounded_rect_alpha(win_x - sp/2, win_y - sp/2 + shadow_layers[i].drop,
                                 win_w + sp, win_h + sp, 12 + sp/2,
                                 0x00000000, shadow_layers[i].alpha);
    }

    // Фон окна — скруглено со всех сторон
    draw_rounded_rect_buf(win_x, win_y, win_w, win_h, 12, BG_MAIN);

    // Шапка окна
    draw_rounded_rect_buf(win_x, win_y, win_w, 36, 12, 0x00E5E5EA);
    draw_rect_buf(win_x, win_y + 18, win_w, 18, 0x00E5E5EA); // сглаживание низа шапки

    // --- "Светофор" в стиле macOS, но скруглёнными квадратами и справа ---
    int tl_size = 14;
    int tl_gap  = 10;
    int tl_y    = win_y + (36 - tl_size) / 2;
    int tl_right_margin = 14;
    // Порядок слева направо как в macOS: закрыть, свернуть, развернуть —
    // просто вся группа целиком прижата к правому краю шапки.
    int close_x    = win_x + win_w - tl_right_margin - tl_size;
    int minimize_x = close_x - tl_gap - tl_size;
    int zoom_x     = minimize_x - tl_gap - tl_size;

    int is_hover_close = (mx >= close_x && mx <= close_x + tl_size && my >= tl_y && my <= tl_y + tl_size);

    uint32_t close_col    = is_hover_close ? 0x00FF6259 : 0x00FF5F57; // ярче при наведении
    uint32_t minimize_col = 0x00FFBD2E; // пока не функциональна
    uint32_t zoom_col     = 0x0028C840; // пока не функциональна

    draw_rounded_rect_buf(zoom_x,     tl_y, tl_size, tl_size, 4, zoom_col);
    draw_rounded_rect_buf(minimize_x, tl_y, tl_size, tl_size, 4, minimize_col);
    draw_rounded_rect_buf(close_x,    tl_y, tl_size, tl_size, 4, close_col);

    if (click && is_hover_close) {
        is_open = 0;
        dragging = 0;
        return;
    }

    // Заголовок / Путь текущей директории
    draw_string(current_dir->name, win_x + 16, win_y + 11, COLOR_TEXT, buf, scr_w);

    // Боковая панель (Sidebar) — скруглена снизу-слева под угол окна (r=12)
    draw_panel_rounded_bottom(win_x, win_y + 36, 140, win_h - 36, 12, BG_SIDEBAR, 1, 0);
    draw_string("PLACES", win_x + 12, win_y + 50, 0x008E8E93, buf, scr_w);

    // Кнопка «Назад»
    if (current_dir->parent != current_dir) {
        draw_rounded_rect_buf(win_x + 8, win_y + 70, 124, 26, 6, 0x00D1D1D6);
        draw_string("< Back", win_x + 16, win_y + 76, COLOR_TEXT, buf, scr_w);
        if (click && mx >= win_x + 8 && mx <= win_x + 132 && my >= win_y + 70 && my <= win_y + 96) {
            current_dir = current_dir->parent;
            selected_idx = -1;
        }
    }

    // Сетка файлов и папок
    int grid_x = win_x + 150;
    int grid_y = win_y + 50;
    int col = 0, row = 0;

    for (int i = 0; i < current_dir->child_count; i++) {
        vfs_node_t* item = current_dir->children[i];
        int ix = grid_x + col * 95;
        int iy = grid_y + row * 80;

        if (mx >= ix && mx <= ix + 85 && my >= iy && my <= iy + 70) {
            if (click) {
                if (selected_idx == i && item->type == NODE_DIR) {
                    if (item->is_real_disk) populate_dir_from_disk(item);
                    current_dir = item;
                    selected_idx = -1;
                    break;
                }
                selected_idx = i;
            }
        }

        if (selected_idx == i) {
            draw_rounded_rect_buf(ix - 2, iy - 2, 89, 74, 8, 0x00007AFF);
            draw_rounded_rect_buf(ix, iy, 85, 70, 6, 0x00FFFFFF);
        }

        uint32_t icon_col = (item->type == NODE_DIR) ? 0x0034C759 : 0x005856D6;
        draw_rounded_rect_buf(ix + 22, iy + 6, 40, 32, 6, icon_col);
        draw_string(item->name, ix + 4, iy + 44, COLOR_TEXT, buf, scr_w);

        col++;
        if (col >= 4) { col = 0; row++; }
    }

    // Правая панель информации (Inspector) — скруглена снизу-справа под угол окна
    int insp_x = win_x + win_w - 200;
    draw_panel_rounded_bottom(insp_x, win_y + 36, 200, win_h - 36, 12, BG_INSPECTOR, 0, 1);

    if (selected_idx >= 0 && selected_idx < current_dir->child_count) {
        vfs_node_t* sel = current_dir->children[selected_idx];

        // Реальный файл с диска — читаем содержимое лениво, один раз при выборе,
        // а не каждый кадр (диск — это не память, каждое чтение стоит времени).
        if (sel->type == NODE_FILE && sel->is_real_disk && sel != last_loaded_file) {
            uint32_t max_len = sizeof(real_file_buf) - 1;
            uint32_t read_bytes = fat32_read_file(sel->fat_cluster, sel->size, real_file_buf, max_len);
            real_file_buf[read_bytes] = 0; // на всякий случай терминируем — draw_string ждёт C-строку
            sel->data = real_file_buf;
            last_loaded_file = sel;
        }

        draw_string("INFO", insp_x + 12, win_y + 50, 0x008E8E93, buf, scr_w);
        draw_string(sel->name, insp_x + 12, win_y + 70, COLOR_TEXT, buf, scr_w);

        char sz_str[16];
        format_size(sel->size, sz_str);
        draw_string("Size:", insp_x + 12, win_y + 100, 0x008E8E93, buf, scr_w);
        draw_string(sz_str, insp_x + 60, win_y + 100, COLOR_TEXT, buf, scr_w);

        if (sel->type == NODE_FILE && sel->data) {
            draw_rounded_rect_buf(insp_x + 10, win_y + 140, 180, 200, 6, 0x00FFFFFF);
            draw_string((const char*)sel->data, insp_x + 16, win_y + 146, 0x003A3A3C, buf, scr_w);
        }
    } else {
        draw_string("No selection", insp_x + 45, win_y + 180, 0x008E8E93, buf, scr_w);
    }
}