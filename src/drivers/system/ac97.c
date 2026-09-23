#include <stdint.h>
#include <stdbool.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

typedef struct {
    uint32_t phys_addr;
    uint16_t samples;
    uint16_t flags;
} __attribute__((packed)) ac97_bdl_entry_t;

static uint16_t nambar  = 0;
static uint16_t nabmbar = 0;

static ac97_bdl_entry_t bdl[32] __attribute__((aligned(16)));

/* Виртуальный адрес ядра -> физический (для DMA). Реализовано в kernel.c
 * через смещение Limine HHDM. */
extern uint64_t kernel_virt_to_phys(uint64_t virt);

static uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) |
                       (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

bool ac97_init(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t vendor_device = pci_read_config(bus, slot, 0, 0x00);
            if ((vendor_device & 0xFFFF) == 0xFFFF) continue;

            uint32_t class_reg = pci_read_config(bus, slot, 0, 0x08);
            uint8_t class_code = (class_reg >> 24) & 0xFF;
            uint8_t subclass   = (class_reg >> 16) & 0xFF;

            if (class_code == 0x04 && subclass == 0x01) {
                nambar  = pci_read_config(bus, slot, 0, 0x10) & 0xFFFE;
                nabmbar = pci_read_config(bus, slot, 0, 0x14) & 0xFFFE;

                uint32_t cmd = pci_read_config(bus, slot, 0, 0x04);
                outl(PCI_CONFIG_ADDRESS, (uint32_t)((bus << 16) | (slot << 11) | 0x80000004));
                outl(PCI_CONFIG_DATA, cmd | 0x05);

                outw(nambar + 0x00, 0x42);
                outb(nabmbar + 0x1B, 0x02);

                outw(nambar + 0x02, 0x0000);
                outw(nambar + 0x18, 0x0000);

                return true;
            }
        }
    }
    return false;
}

void ac97_set_volume(uint8_t volume) {
    if (!nambar) return;
    if (volume > 100) volume = 100;
    uint8_t atten = 31 - (volume * 31 / 100);
    uint16_t val = (atten << 8) | atten;
    outw(nambar + 0x02, val);
    outw(nambar + 0x18, val);
}

void ac97_play_pcm(const uint8_t *pcm_data, uint32_t length) {
    if (!nabmbar || !pcm_data || length == 0) return;

    /* Останавливаем текущее воспроизведение, пока перепрограммируем BDL. */
    outb(nabmbar + 0x1B, 0x00);

    /*
     * Zero-copy: дескрипторы указывают прямо на переданный буфер
     * (в т.ч. на вшитые в ядро WAV-дорожки). Один дескриптор описывает
     * не более 65535 сэмплов (поле samples 16-битное), поэтому длинный
     * буфер разбивается на цепочку до 32 дескрипторов.
     */
    uint32_t off = 0;
    int n = 0;

    while (off < length && n < 32) {
        uint32_t chunk = length - off;
        if (chunk > 65535u * 2) chunk = 65535u * 2;
        chunk &= ~3u; /* чётное число 16-битных сэмплов */
        if (chunk == 0) break;

        bdl[n].phys_addr = (uint32_t)kernel_virt_to_phys((uint64_t)(uintptr_t)(pcm_data + off));
        bdl[n].samples   = (uint16_t)(chunk / 2);
        bdl[n].flags     = 0; /* без прерываний: статус не опрашиваем */
        off += chunk;
        n++;
    }

    if (n == 0) return;

    bdl[n - 1].flags = 0x8000; /* IOC на последнем дескрипторе */

    outb(nabmbar + 0x1B, 0x02);               /* сброс DMA-движка */
    outl(nabmbar + 0x10, (uint32_t)kernel_virt_to_phys((uint64_t)(uintptr_t)bdl));
    outb(nabmbar + 0x15, (uint8_t)(n - 1));   /* LVI: индекс последнего */
    outw(nabmbar + 0x16, 0xFFFF);             /* очистка флагов статуса */
    outb(nabmbar + 0x1B, 0x01);               /* запуск */
}
