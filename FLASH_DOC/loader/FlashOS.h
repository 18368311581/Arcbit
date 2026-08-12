#ifndef FLASH_OS_H
#define FLASH_OS_H

#define VERS 1
#define EXTSPI 5
#define SECTOR_NUM 512
#define FLASH_DRV_VERS (0x0100 + VERS)
#define SECTOR_END 0xFFFFFFFF, 0xFFFFFFFF

struct FlashSectors {
    unsigned long szSector;
    unsigned long AddrSector;
};

struct FlashDevice {
    unsigned short Vers;
    char DevName[128];
    unsigned short DevType;
    unsigned long DevAdr;
    unsigned long szDev;
    unsigned long szPage;
    unsigned long Res;
    unsigned char valEmpty;
    unsigned long toProg;
    unsigned long toErase;
    struct FlashSectors sectors[SECTOR_NUM];
};

#endif
