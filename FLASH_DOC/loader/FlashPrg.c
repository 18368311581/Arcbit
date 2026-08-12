#include "FlashOS.h"

#define FLASH_BASE             0x90000000UL
#define FLASH_SIZE             0x00800000UL

#define RCC_BASE               0x40023800UL
#define GPIOB_BASE             0x40020400UL
#define SPI2_BASE              0x40003800UL

#define REG32(address)         (*(volatile unsigned long *)(address))
#define REG8(address)          (*(volatile unsigned char *)(address))

#define RCC_CR                 REG32(RCC_BASE + 0x00UL)
#define RCC_CFGR               REG32(RCC_BASE + 0x08UL)
#define RCC_APB1RSTR           REG32(RCC_BASE + 0x20UL)
#define RCC_AHB1ENR            REG32(RCC_BASE + 0x30UL)
#define RCC_APB1ENR            REG32(RCC_BASE + 0x40UL)

#define GPIOB_MODER            REG32(GPIOB_BASE + 0x00UL)
#define GPIOB_OTYPER           REG32(GPIOB_BASE + 0x04UL)
#define GPIOB_OSPEEDR          REG32(GPIOB_BASE + 0x08UL)
#define GPIOB_PUPDR            REG32(GPIOB_BASE + 0x0CUL)
#define GPIOB_BSRR             REG32(GPIOB_BASE + 0x18UL)
#define GPIOB_AFRH             REG32(GPIOB_BASE + 0x24UL)

#define SPI2_CR1               REG32(SPI2_BASE + 0x00UL)
#define SPI2_CR2               REG32(SPI2_BASE + 0x04UL)
#define SPI2_SR                REG32(SPI2_BASE + 0x08UL)
#define SPI2_DR8               REG8(SPI2_BASE + 0x0CUL)

#define CMD_READ_ID            0x9FU
#define CMD_READ_STATUS1       0x05U
#define CMD_WRITE_ENABLE       0x06U
#define CMD_READ               0x03U
#define CMD_PAGE_PROGRAM       0x02U
#define CMD_SECTOR_ERASE       0x20U

#define SPI_WAIT_LIMIT         1000000UL
#define PROGRAM_WAIT_LIMIT     2000000UL
#define ERASE_WAIT_LIMIT       20000000UL

static void cs_low(void) {
    GPIOB_BSRR = 1UL << (12U + 16U);
}

static void cs_high(void) {
    GPIOB_BSRR = 1UL << 12U;
}

static int spi_transfer(unsigned char output, unsigned char *input) {
    unsigned long timeout = SPI_WAIT_LIMIT;
    while ((SPI2_SR & (1UL << 1U)) == 0U) {
        if (--timeout == 0U) {
            return 1;
        }
    }
    SPI2_DR8 = output;
    timeout = SPI_WAIT_LIMIT;
    while ((SPI2_SR & 1UL) == 0U) {
        if (--timeout == 0U) {
            return 1;
        }
    }
    *input = SPI2_DR8;
    return 0;
}

static int send_byte(unsigned char value) {
    unsigned char ignored;
    return spi_transfer(value, &ignored);
}

static int read_status(unsigned char *status) {
    int error;
    cs_low();
    error = send_byte(CMD_READ_STATUS1);
    if (error == 0) {
        error = spi_transfer(0xFFU, status);
    }
    cs_high();
    return error;
}

static int wait_ready(unsigned long limit) {
    unsigned char status;
    while (limit-- != 0U) {
        if (read_status(&status) != 0) {
            return 1;
        }
        if ((status & 1U) == 0U) {
            return 0;
        }
    }
    return 1;
}

static int write_enable(void) {
    unsigned char status;
    cs_low();
    if (send_byte(CMD_WRITE_ENABLE) != 0) {
        cs_high();
        return 1;
    }
    cs_high();
    if (read_status(&status) != 0) {
        return 1;
    }
    return (status & 2U) == 0U;
}

static int send_address(unsigned long address) {
    if (send_byte((unsigned char)(address >> 16U)) != 0) {
        return 1;
    }
    if (send_byte((unsigned char)(address >> 8U)) != 0) {
        return 1;
    }
    return send_byte((unsigned char)address);
}

static int begin_read(unsigned long offset) {
    cs_low();
    if (send_byte(CMD_READ) != 0 || send_address(offset) != 0) {
        cs_high();
        return 1;
    }
    return 0;
}

int Init(unsigned long adr, unsigned long clk, unsigned long fnc) {
    unsigned long timeout;
    unsigned char id[3];
    unsigned int i;

    (void)adr;
    (void)clk;
    (void)fnc;

    RCC_CR |= 1UL;
    timeout = SPI_WAIT_LIMIT;
    while ((RCC_CR & (1UL << 1U)) == 0U) {
        if (--timeout == 0U) {
            return 1;
        }
    }
    RCC_CFGR &= ~3UL;
    timeout = SPI_WAIT_LIMIT;
    while ((RCC_CFGR & (3UL << 2U)) != 0U) {
        if (--timeout == 0U) {
            return 1;
        }
    }

    RCC_AHB1ENR |= 1UL << 1U;
    RCC_APB1ENR |= 1UL << 14U;
    RCC_APB1RSTR |= 1UL << 14U;
    RCC_APB1RSTR &= ~(1UL << 14U);

    cs_high();
    GPIOB_MODER = (GPIOB_MODER & ~0xFF000000UL)
        | (1UL << 24U) | (2UL << 26U) | (2UL << 28U) | (2UL << 30U);
    GPIOB_OTYPER &= ~(0xFUL << 12U);
    GPIOB_OSPEEDR |= 0xFF000000UL;
    GPIOB_PUPDR = (GPIOB_PUPDR & ~0xFF000000UL) | (1UL << 24U);
    GPIOB_AFRH = (GPIOB_AFRH & ~0xFFF00000UL)
        | (5UL << 20U) | (5UL << 24U) | (5UL << 28U);

    SPI2_CR1 = 0U;
    SPI2_CR2 = 0U;
    SPI2_CR1 = (1UL << 9U) | (1UL << 8U) | (1UL << 6U) | (1UL << 3U) | (1UL << 2U);

    cs_low();
    if (send_byte(CMD_READ_ID) != 0) {
        cs_high();
        return 1;
    }
    for (i = 0U; i < 3U; i++) {
        if (spi_transfer(0xFFU, &id[i]) != 0) {
            cs_high();
            return 1;
        }
    }
    cs_high();
    return id[2] == 0x17U ? 0 : 1;
}

int UnInit(unsigned long fnc) {
    (void)fnc;
    cs_high();
    SPI2_CR1 &= ~(1UL << 6U);
    return 0;
}

int EraseSector(unsigned long adr) {
    unsigned long offset;
    if (adr < FLASH_BASE || adr >= FLASH_BASE + FLASH_SIZE) {
        return 1;
    }
    offset = (adr - FLASH_BASE) & ~0xFFFUL;
    if (wait_ready(PROGRAM_WAIT_LIMIT) != 0 || write_enable() != 0) {
        return 1;
    }
    cs_low();
    if (send_byte(CMD_SECTOR_ERASE) != 0 || send_address(offset) != 0) {
        cs_high();
        return 1;
    }
    cs_high();
    return wait_ready(ERASE_WAIT_LIMIT);
}

int ProgramPage(unsigned long adr, unsigned long sz, unsigned char *buf) {
    unsigned long offset;
    unsigned long i;
    if (adr < FLASH_BASE || sz == 0U || sz > 256U || adr + sz > FLASH_BASE + FLASH_SIZE) {
        return 1;
    }
    offset = adr - FLASH_BASE;
    if ((offset & 0xFFU) + sz > 256U) {
        return 1;
    }
    if (wait_ready(PROGRAM_WAIT_LIMIT) != 0 || write_enable() != 0) {
        return 1;
    }
    cs_low();
    if (send_byte(CMD_PAGE_PROGRAM) != 0 || send_address(offset) != 0) {
        cs_high();
        return 1;
    }
    for (i = 0U; i < sz; i++) {
        if (send_byte(buf[i]) != 0) {
            cs_high();
            return 1;
        }
    }
    cs_high();
    return wait_ready(PROGRAM_WAIT_LIMIT);
}

int BlankCheck(unsigned long adr, unsigned long sz, unsigned char pat) {
    unsigned long offset;
    unsigned long i;
    unsigned char value;
    if (adr < FLASH_BASE || adr + sz > FLASH_BASE + FLASH_SIZE) {
        return 1;
    }
    offset = adr - FLASH_BASE;
    if (wait_ready(PROGRAM_WAIT_LIMIT) != 0 || begin_read(offset) != 0) {
        return 1;
    }
    for (i = 0U; i < sz; i++) {
        if (spi_transfer(0xFFU, &value) != 0 || value != pat) {
            cs_high();
            return 1;
        }
    }
    cs_high();
    return 0;
}

unsigned long Verify(unsigned long adr, unsigned long sz, unsigned char *buf) {
    unsigned long offset;
    unsigned long i;
    unsigned char value;
    if (adr < FLASH_BASE || adr + sz > FLASH_BASE + FLASH_SIZE) {
        return adr;
    }
    offset = adr - FLASH_BASE;
    if (wait_ready(PROGRAM_WAIT_LIMIT) != 0 || begin_read(offset) != 0) {
        return adr;
    }
    for (i = 0U; i < sz; i++) {
        if (spi_transfer(0xFFU, &value) != 0 || value != buf[i]) {
            cs_high();
            return adr + i;
        }
    }
    cs_high();
    return adr + sz;
}
