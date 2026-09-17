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

#define GCAP     0x00
#define GCTL     0x08
#define STATESTS 0x0E

static uint64_t hda_mmio_base = 0;

static uint32_t pci_read_config(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (slot << 11) |
                       (func << 8) | (offset & 0xFC) | ((uint32_t)0x80000000));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static inline void hda_write32(uint32_t reg, uint32_t value) {
    *(volatile uint32_t*)(unsigned long)(hda_mmio_base + reg) = value;
}

static inline uint32_t hda_read32(uint32_t reg) {
    return *(volatile uint32_t*)(unsigned long)(hda_mmio_base + reg);
}

static inline uint16_t hda_read16(uint32_t reg) {
    return *(volatile uint16_t*)(unsigned long)(hda_mmio_base + reg);
}

bool hda_init(void) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t vendor_device = pci_read_config(bus, slot, 0, 0x00);
            if ((vendor_device & 0xFFFF) == 0xFFFF) continue;

            uint32_t class_reg = pci_read_config(bus, slot, 0, 0x08);
            uint8_t class_code = (class_reg >> 24) & 0xFF;
            uint8_t subclass   = (class_reg >> 16) & 0xFF;

            if (class_code == 0x04 && subclass == 0x03) {
                uint32_t bar0 = pci_read_config(bus, slot, 0, 0x10);
                uint32_t bar1 = pci_read_config(bus, slot, 0, 0x14);

                hda_mmio_base = (uint64_t)(bar0 & 0xFFFFFFF0);
                if ((bar0 & 0x06) == 0x04) {
                    hda_mmio_base |= (((uint64_t)bar1) << 32);
                }

                uint32_t cmd = pci_read_config(bus, slot, 0, 0x04);
                outl(PCI_CONFIG_ADDRESS, (uint32_t)((bus << 16) | (slot << 11) | 0x80000004));
                outl(PCI_CONFIG_DATA, cmd | 0x06);

                uint32_t gctl_val = hda_read32(GCTL);
                hda_write32(GCTL, gctl_val & ~0x01);

                for (volatile int i = 0; i < 10000; i++);

                hda_write32(GCTL, gctl_val | 0x01);

                uint32_t timeout = 100000;
                while (!(hda_read32(GCTL) & 0x01) && timeout--) {
                    __asm__ volatile ("pause");
                }

                uint16_t statests = hda_read16(STATESTS);
                if (statests == 0) {
                    return false;
                }

                return true;
            }
        }
    }
    return false;
}

void hda_set_volume(uint8_t volume) {
    (void)volume;
}

void hda_play_pcm(const uint8_t *pcm_data, uint32_t length) {
    (void)pcm_data;
    (void)length;
}
