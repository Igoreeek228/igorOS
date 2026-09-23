/* file_manager.c — Finder-like (macOS) file browser for IgorOS Nord */

#include "gui/apps/file/file_manager.h"
#include "gui/desktop.h"
#include "gui/font.h"
#include "gui/anim/genie_anim.h"
#include "gui/anim/win_chrome.h"
#include "drivers/system/fat32.h"
#include "gui/apps/doom/doom_fs_api.h"

extern void draw_rounded_rect_buf(int x, int y, int w, int h, int r, uint32_t color);
extern void draw_rounded_rect_alpha(int x, int y, int w, int h, int r, uint32_t color, uint8_t alpha);
extern void draw_rect_buf(int x, int y, int w, int h, uint32_t color);
extern void draw_pixel_buf(int x, int y, uint32_t color);

#define FILE_DOCK_INDEX 0
#define HEADER_H 52
#define SIDEBAR_W 160

static int is_open = 0, minimized = 0;
static int win_x = 120, win_y = 70, win_w = 780, win_h = 480;
static int dragging = 0, drag_ox = 0, drag_oy = 0;
static genie_state_t genie;

static vfs_node_t root_dir, docs_dir, media_dir, sample_txt;
static vfs_node_t* current_dir = &root_dir;
static int selected_idx = -1;

static vfs_node_t disk_root;
static int disk_available = 0;
static vfs_node_t disk_pool[64];
static int disk_pool_used = 0;

static vfs_node_t* alloc_disk_node(void) {
    if (disk_pool_used >= 64) return 0;
    return &disk_pool[disk_pool_used++];
}

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
        node->data = 0;
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
    int i = 0;
    if (bytes >= 1024 * 1024) {
        uint32_t mb = bytes / (1024 * 1024);
        uint32_t frac = (bytes / 1024) % 1024 / 102; /* ~0.1 MB digit */
        if (mb >= 100) { out[i++] = '0' + (mb / 100) % 10; }
        if (mb >= 10)  { out[i++] = '0' + (mb / 10) % 10; }
        out[i++] = '0' + (mb % 10);
        out[i++] = '.';
        out[i++] = '0' + (frac % 10);
        out[i++] = ' '; out[i++] = 'M'; out[i++] = 'B'; out[i] = 0;
    } else if (bytes >= 1024) {
        uint32_t kb = bytes / 1024;
        if (kb >= 100) { out[i++] = '0' + (kb / 100) % 10; }
        if (kb >= 10)  { out[i++] = '0' + (kb / 10) % 10; }
        out[i++] = '0' + (kb % 10);
        out[i++] = ' '; out[i++] = 'K'; out[i++] = 'B'; out[i] = 0;
    } else {
        if (bytes >= 100) { out[i++] = '0' + (bytes / 100) % 10; }
        if (bytes >= 10)  { out[i++] = '0' + (bytes / 10) % 10; }
        out[i++] = '0' + (bytes % 10);
        out[i++] = ' '; out[i++] = 'B'; out[i] = 0;
    }
}

/* Virtual C: drive with doom/ + DOOM.WAD + saves */
static vfs_node_t c_drive;
static vfs_node_t doom_dir;
static vfs_node_t doom_wad_file;
static vfs_node_t doom_save_nodes[8];
static int c_inited = 0;

static void name_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i + 1 < max) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void refresh_doom_folder(void) {
    /* DOOM.WAD always first */
    doom_dir.child_count = 0;
    doom_wad_file.type = NODE_FILE;
    doom_wad_file.parent = &doom_dir;
    doom_wad_file.data = 0;
    doom_wad_file.is_real_disk = 0;
    doom_wad_file.populated = 1;
    name_copy(doom_wad_file.name, "DOOM.WAD", 32);
    doom_wad_file.size = (uint32_t)doom_fs_wad_size();
    doom_dir.children[doom_dir.child_count++] = &doom_wad_file;

    char names[8][64];
    uint32_t sizes[8];
    int n = doom_fs_list_saves(names, sizes, 8);
    for (int i = 0; i < n && doom_dir.child_count < 16; i++) {
        vfs_node_t *node = &doom_save_nodes[i];
        node->type = NODE_FILE;
        node->parent = &doom_dir;
        node->data = 0;
        node->is_real_disk = 0;
        node->populated = 1;
        node->size = sizes[i];
        name_copy(node->name, names[i], 32);
        doom_dir.children[doom_dir.child_count++] = node;
    }
    doom_dir.populated = 1;
}

void init_file_manager(void) {
    if (c_inited) {
        refresh_doom_folder();
        return;
    }
    c_inited = 1;

    /* Root = computer */
    root_dir.type = NODE_DIR;
    root_dir.parent = &root_dir;
    root_dir.child_count = 0;
    root_dir.is_real_disk = 0;
    root_dir.populated = 1;
    name_copy(root_dir.name, "This PC", 32);

    /* C: drive — default home */
    c_drive.type = NODE_DIR;
    c_drive.parent = &root_dir;
    c_drive.child_count = 0;
    c_drive.is_real_disk = 0;
    c_drive.populated = 1;
    name_copy(c_drive.name, "C:", 32);

    /* C:\\Documents */
    docs_dir.type = NODE_DIR;
    docs_dir.parent = &c_drive;
    docs_dir.child_count = 0;
    docs_dir.is_real_disk = 0;
    docs_dir.populated = 1;
    name_copy(docs_dir.name, "Documents", 32);

    sample_txt.type = NODE_FILE;
    sample_txt.size = sizeof(text_data);
    sample_txt.data = (const uint8_t*)text_data;
    sample_txt.parent = &docs_dir;
    sample_txt.is_real_disk = 0;
    name_copy(sample_txt.name, "readme.txt", 32);
    docs_dir.children[0] = &sample_txt;
    docs_dir.child_count = 1;

    /* C:\\doom */
    doom_dir.type = NODE_DIR;
    doom_dir.parent = &c_drive;
    doom_dir.child_count = 0;
    doom_dir.is_real_disk = 0;
    doom_dir.populated = 0;
    name_copy(doom_dir.name, "doom", 32);
    refresh_doom_folder();

    c_drive.children[0] = &docs_dir;
    c_drive.children[1] = &doom_dir;
    c_drive.child_count = 2;

    root_dir.children[0] = &c_drive;
    root_dir.child_count = 1;

    /* Optional real FAT32 volume as D: if present */
    disk_available = fat32_init();
    if (disk_available) {
        disk_root.type = NODE_DIR;
        disk_root.parent = &root_dir;
        disk_root.child_count = 0;
        disk_root.fat_cluster = fat32_root_cluster();
        disk_root.is_real_disk = 1;
        disk_root.populated = 0;
        name_copy(disk_root.name, "D:", 32);
        populate_dir_from_disk(&disk_root);
        root_dir.children[root_dir.child_count++] = &disk_root;
    }

    current_dir = &c_drive; /* open on C: by default */
}

void toggle_file_manager(void) {
    init_file_manager();
    int dx, dy;
    genie_dock_icon_point(FILE_DOCK_INDEX, &dx, &dy);
    if (genie_is_animating(&genie)) genie_cancel(&genie);

    if (is_open && minimized) {
        genie_start_open(&genie, dx, dy);
        minimized = 0; dragging = 0;
        return;
    }
    if (is_open) {
        genie_start_close(&genie, dx, dy);
        is_open = 0; minimized = 0; dragging = 0;
        return;
    }
    is_open = 1; minimized = 0; dragging = 0;
    genie_start_open(&genie, dx, dy);
}

void render_file_manager_window(uint32_t* buf, int scr_w, int scr_h, int mx, int my, int btn, int click) {
    (void)scr_h;
    genie_tick(&genie);
    if (!is_open && !genie_is_animating(&genie)) return;
    if (minimized && !genie_is_animating(&genie)) return;

    int mid = genie_is_animating(&genie);
    int traffic_zone = 90;

    /* traffic lights on the right — exclude that zone from drag */
    if (!mid && btn && !dragging && win_drag_available() &&
        mx >= win_x && mx <= win_x + win_w - traffic_zone &&
        my >= win_y && my <= win_y + HEADER_H) {
        dragging = 1; drag_ox = mx - win_x; drag_oy = my - win_y;
        win_drag_claim();
        win_set_focused(WIN_ID_FILE_);
    }
    if (!btn) dragging = 0;
    if (dragging) { win_x = mx - drag_ox; win_y = my - drag_oy; }

    win_report_rect(WIN_ID_FILE_, win_x, win_y, win_w, win_h, is_open && !mid);
    int occluded = win_click_occluded(WIN_ID_FILE_, mx, my);

    int dx, dy, dw, dh;
    genie_get_rect(&genie, win_x, win_y, win_w, win_h, &dx, &dy, &dw, &dh);

    /* light shadow */
    draw_rounded_rect_alpha(dx - 4, dy + 4, dw + 8, dh + 6, 14, 0, 28);

    /* Window body — macOS Finder light gray */
    draw_rounded_rect_buf(dx, dy, dw, dh, 12, 0x00E8E8ED);

    if (mid) return;

    /* Toolbar / title bar */
    draw_rounded_rect_buf(win_x, win_y, win_w, HEADER_H, 12, 0x00F0F0F5);
    draw_rect_buf(win_x, win_y + HEADER_H / 2, win_w, HEADER_H / 2, 0x00F0F0F5);
    draw_rect_buf(win_x, win_y + HEADER_H - 1, win_w, 1, 0x00D0D0D6);

    int min_c = 0, zoom_c = 0;
    if (win_chrome_traffic_lights(WIN_ID_FILE_, win_x, win_y, win_w, HEADER_H,
                                  mx, my, click, occluded, &min_c, &zoom_c)) {
        int gx, gy;
        genie_dock_icon_point(FILE_DOCK_INDEX, &gx, &gy);
        genie_start_close(&genie, gx, gy);
        is_open = 0; dragging = 0;
        return;
    }
    if (min_c) {
        int gx, gy;
        genie_dock_icon_point(FILE_DOCK_INDEX, &gx, &gy);
        genie_start_minimize(&genie, gx, gy);
        minimized = 1; dragging = 0;
        return;
    }

    /* Path title centered-ish */
    draw_string(current_dir->name, win_x + 100, win_y + 18, 0x001C1C1E, buf, (uint32_t)scr_w);

    /* Sidebar */
    draw_rect_buf(win_x, win_y + HEADER_H, SIDEBAR_W, win_h - HEADER_H - 12, 0x00E5E5EA);
    draw_rect_buf(win_x + SIDEBAR_W, win_y + HEADER_H, 1, win_h - HEADER_H, 0x00D0D0D6);
    draw_string("Favorites", win_x + 16, win_y + HEADER_H + 14, 0x008E8E93, buf, (uint32_t)scr_w);

    /* Places in sidebar */
    for (int i = 0; i < root_dir.child_count; i++) {
        vfs_node_t* pl = root_dir.children[i];
        int sy = win_y + HEADER_H + 40 + i * 28;
        int sel = (current_dir == pl) || (current_dir->parent == pl);
        if (sel) draw_rounded_rect_buf(win_x + 8, sy - 4, SIDEBAR_W - 16, 24, 6, 0x00D1D1D6);
        draw_string(pl->name, win_x + 16, sy, 0x001C1C1E, buf, (uint32_t)scr_w);
        if (click && !occluded && mx >= win_x + 8 && mx <= win_x + SIDEBAR_W - 8 &&
            my >= sy - 4 && my <= sy + 20) {
            if (pl->is_real_disk) populate_dir_from_disk(pl);
            if (pl == &doom_dir) refresh_doom_folder();
            current_dir = pl;
            selected_idx = -1;
            win_set_focused(WIN_ID_FILE_);
        }
    }

    /* Back control */
    if (current_dir->parent != current_dir) {
        int bx = win_x + SIDEBAR_W + 16, by = win_y + HEADER_H + 12;
        draw_rounded_rect_buf(bx, by, 64, 24, 6, 0x00FFFFFF);
        draw_string("< Back", bx + 8, by + 4, 0x001C1C1E, buf, (uint32_t)scr_w);
        if (click && !occluded && mx >= bx && mx <= bx + 64 && my >= by && my <= by + 24) {
            current_dir = current_dir->parent;
            selected_idx = -1;
        }
    }

    /* Content area — white */
    int cx0 = win_x + SIDEBAR_W + 1;
    int cy0 = win_y + HEADER_H;
    draw_rect_buf(cx0, cy0, win_w - SIDEBAR_W - 1, win_h - HEADER_H - 12, 0x00FFFFFF);

    int grid_x = cx0 + 16;
    int grid_y = cy0 + 48;
    int col = 0, row = 0;
    for (int i = 0; i < current_dir->child_count; i++) {
        vfs_node_t* item = current_dir->children[i];
        int ix = grid_x + col * 100;
        int iy = grid_y + row * 88;
        int hover = mx >= ix && mx <= ix + 90 && my >= iy && my <= iy + 78;
        int sel = (selected_idx == i);

        if (sel || hover)
            draw_rounded_rect_buf(ix, iy, 90, 78, 10, sel ? 0x00B3D7FF : 0x00F0F0F5);

        /* icon tile */
        draw_rounded_rect_buf(ix + 21, iy + 8, 48, 40, 8,
            item->type == NODE_DIR ? 0x005AC8FA : 0x00FFD60A);

        draw_string(item->name, ix + 4, iy + 54, 0x001C1C1E, buf, (uint32_t)scr_w);

        if (click && hover && !occluded) {
            win_set_focused(WIN_ID_FILE_);
            if (selected_idx == i && item->type == NODE_DIR) {
                if (item->is_real_disk) populate_dir_from_disk(item);
                if (item == &doom_dir) refresh_doom_folder();
                current_dir = item;
                selected_idx = -1;
            } else {
                selected_idx = i;
            }
        }

        col++;
        if (col >= 5) { col = 0; row++; }
    }

    /* Inspector strip bottom of content if selection */
    if (selected_idx >= 0 && selected_idx < current_dir->child_count) {
        vfs_node_t* sel = current_dir->children[selected_idx];
        char sz[12];
        format_size(sel->size, sz);
        int iy = win_y + win_h - 36;
        draw_rect_buf(cx0, iy, win_w - SIDEBAR_W - 1, 24, 0x00F5F5F7);
        draw_string(sel->name, cx0 + 12, iy + 4, 0x001C1C1E, buf, (uint32_t)scr_w);
        if (sel->type == NODE_FILE)
            draw_string(sz, cx0 + 200, iy + 4, 0x008E8E93, buf, (uint32_t)scr_w);
    }
}
