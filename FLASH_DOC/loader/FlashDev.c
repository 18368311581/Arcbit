#include "FlashOS.h"

struct FlashDevice const FlashDevice = {
    FLASH_DRV_VERS,
    "W25Q64 on STM32F411 SPI2",
    EXTSPI,
    0x90000000,
    0x00800000,
    256,
    0,
    0xFF,
    1000,
    10000,
    0x001000, 0x000000,
    SECTOR_END
};
