#ifndef __FLASH_H__
#define __FLASH_H__
#ifdef __cplusplus
extern "C" {
#endif
#include "basic.h"

/* W25Q64 使用 SPI2 Mode 0，CS 低有效，容量 8 MB。 */
#define FLASH_CS_Pin            GPIO_PIN_12
#define FLASH_CS_GPIO_Port      GPIOB
#define FLASH_SCK_Pin           GPIO_PIN_13
#define FLASH_SCK_GPIO_Port     GPIOB
#define FLASH_MISO_Pin          GPIO_PIN_14
#define FLASH_MISO_GPIO_Port    GPIOB
#define FLASH_MOSI_Pin          GPIO_PIN_15
#define FLASH_MOSI_GPIO_Port    GPIOB
#define FLASH_SIZE              0x800000UL
#define FLASH_SECTOR_SIZE       4096U
#define FLASH_PAGE_SIZE         256U

/* 初始化 SPI2 和 Flash CS。返回 0 成功，非零失败。 */
uint8_t Flash_init(void);
/* 读取 24 位 JEDEC ID 或状态寄存器 1。 */
uint8_t Flash_read_jedec_id(uint32_t* jedec_id);
uint8_t Flash_read_status(uint8_t* status);
/* 在 8 MB 地址范围内读取任意长度数据。 */
uint8_t Flash_read(uint32_t address,uint8_t* data,uint32_t length);
/* 擦除 address 所在的 4 KB 扇区。 */
uint8_t Flash_erase_sector(uint32_t address);
/* 任意长度写入；需要 0→1 改写时自动执行扇区读改擦写。 */
uint8_t Flash_write(uint32_t address,const uint8_t* data,uint32_t length);
#ifdef __cplusplus
}
#endif
#endif
