#include "flash.h"

#define FLASH_CMD_READ_ID       0x9FU
#define FLASH_CMD_READ_STATUS1  0x05U
#define FLASH_CMD_WRITE_ENABLE  0x06U
#define FLASH_CMD_READ          0x03U
#define FLASH_CMD_PAGE_PROGRAM  0x02U
#define FLASH_CMD_SECTOR_ERASE  0x20U
#define FLASH_SPI_TIMEOUT       100U
#define FLASH_PROGRAM_TIMEOUT   500U
#define FLASH_ERASE_TIMEOUT     5000U

static SPI_HandleTypeDef flash_spi;
static uint8_t flash_sector_buffer[FLASH_SECTOR_SIZE];

static void flash_cs_low(void){
    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,GPIO_PIN_RESET);
}

static void flash_cs_high(void){
    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,GPIO_PIN_SET);
}

static uint8_t flash_transmit(const uint8_t* data,uint16_t length){
    return HAL_SPI_Transmit(&flash_spi,(uint8_t*)data,length,FLASH_SPI_TIMEOUT)==HAL_OK?0U:1U;
}

static uint8_t flash_receive(uint8_t* data,uint16_t length){
    return HAL_SPI_Receive(&flash_spi,data,length,FLASH_SPI_TIMEOUT)==HAL_OK?0U:1U;
}

static void flash_address_bytes(uint8_t* command,uint32_t address){
    command[1]=(uint8_t)(address>>16);
    command[2]=(uint8_t)(address>>8);
    command[3]=(uint8_t)address;
}

static uint8_t flash_wait_ready(uint32_t timeout){
    uint32_t start=HAL_GetTick();
    uint8_t status;
    do{
        if(Flash_read_status(&status)!=0U){return 1U;}
        if((status&0x01U)==0U){return 0U;}
    }while((uint32_t)(HAL_GetTick()-start)<timeout);
    return 1U;
}

/* 每次编程或擦除前发送 WREN，并确认状态寄存器中的 WEL 位。 */
static uint8_t flash_write_enable(void){
    uint8_t command=FLASH_CMD_WRITE_ENABLE;
    uint8_t status;
    flash_cs_low();
    if(flash_transmit(&command,1U)!=0U){
        flash_cs_high();
        return 1U;
    }
    flash_cs_high();
    if(Flash_read_status(&status)!=0U){return 1U;}
    return (status&0x02U)!=0U?0U:1U;
}

static uint8_t flash_program_pages(uint32_t address,const uint8_t* data,uint32_t length){
    uint8_t command[4];
    uint32_t chunk;
    while(length!=0U){
        /* Page Program 不允许跨越 256 字节页边界。 */
        chunk=FLASH_PAGE_SIZE-(address&(FLASH_PAGE_SIZE-1U));
        if(chunk>length){chunk=length;}
        if(flash_wait_ready(FLASH_PROGRAM_TIMEOUT)!=0U){return 1U;}
        if(flash_write_enable()!=0U){return 1U;}
        command[0]=FLASH_CMD_PAGE_PROGRAM;
        flash_address_bytes(command,address);
        flash_cs_low();
        if(flash_transmit(command,4U)!=0U||flash_transmit(data,(uint16_t)chunk)!=0U){
            flash_cs_high();
            return 1U;
        }
        flash_cs_high();
        if(flash_wait_ready(FLASH_PROGRAM_TIMEOUT)!=0U){return 1U;}
        address+=chunk;
        data+=chunk;
        length-=chunk;
    }
    return 0U;
}

uint8_t Flash_init(void){
    GPIO_InitTypeDef gpio={0};
    HAL_GPIO_WritePin(FLASH_CS_GPIO_Port,FLASH_CS_Pin,GPIO_PIN_SET);
    gpio.Pin=FLASH_CS_Pin;
    gpio.Mode=GPIO_MODE_OUTPUT_PP;
    gpio.Pull=GPIO_PULLUP;
    gpio.Speed=GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(FLASH_CS_GPIO_Port,&gpio);
    gpio.Pin=FLASH_SCK_Pin|FLASH_MISO_Pin|FLASH_MOSI_Pin;
    gpio.Mode=GPIO_MODE_AF_PP;
    gpio.Pull=GPIO_PULLUP;
    gpio.Speed=GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate=GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB,&gpio);
    flash_spi.Instance=SPI2;
    flash_spi.Init.Mode=SPI_MODE_MASTER;
    flash_spi.Init.Direction=SPI_DIRECTION_2LINES;
    flash_spi.Init.DataSize=SPI_DATASIZE_8BIT;
    flash_spi.Init.CLKPolarity=SPI_POLARITY_LOW;
    flash_spi.Init.CLKPhase=SPI_PHASE_1EDGE;
    flash_spi.Init.NSS=SPI_NSS_SOFT;
    flash_spi.Init.BaudRatePrescaler=SPI_BAUDRATEPRESCALER_4;
    flash_spi.Init.FirstBit=SPI_FIRSTBIT_MSB;
    flash_spi.Init.TIMode=SPI_TIMODE_DISABLE;
    flash_spi.Init.CRCCalculation=SPI_CRCCALCULATION_DISABLE;
    flash_spi.Init.CRCPolynomial=7U;
    return HAL_SPI_Init(&flash_spi)==HAL_OK?0U:1U;
}

uint8_t Flash_read_jedec_id(uint32_t* jedec_id){
    uint8_t command=FLASH_CMD_READ_ID;
    uint8_t id[3];
    if(jedec_id==NULL){return 1U;}
    flash_cs_low();
    if(flash_transmit(&command,1U)!=0U||flash_receive(id,3U)!=0U){
        flash_cs_high();
        return 1U;
    }
    flash_cs_high();
    *jedec_id=((uint32_t)id[0]<<16)|((uint32_t)id[1]<<8)|id[2];
    return 0U;
}

uint8_t Flash_read_status(uint8_t* status){
    uint8_t command=FLASH_CMD_READ_STATUS1;
    if(status==NULL){return 1U;}
    flash_cs_low();
    if(flash_transmit(&command,1U)!=0U||flash_receive(status,1U)!=0U){
        flash_cs_high();
        return 1U;
    }
    flash_cs_high();
    return 0U;
}

uint8_t Flash_read(uint32_t address,uint8_t* data,uint32_t length){
    uint8_t command[4];
    uint16_t chunk;
    if(length==0U){return 0U;}
    if(data==NULL||address>=FLASH_SIZE||length>FLASH_SIZE-address){return 1U;}
    if(flash_wait_ready(FLASH_PROGRAM_TIMEOUT)!=0U){return 1U;}
    command[0]=FLASH_CMD_READ;
    flash_address_bytes(command,address);
    flash_cs_low();
    if(flash_transmit(command,4U)!=0U){
        flash_cs_high();
        return 1U;
    }
    while(length!=0U){
        chunk=length>65535U?65535U:(uint16_t)length;
        if(flash_receive(data,chunk)!=0U){
            flash_cs_high();
            return 1U;
        }
        data+=chunk;
        length-=chunk;
    }
    flash_cs_high();
    return 0U;
}

uint8_t Flash_erase_sector(uint32_t address){
    uint8_t command[4];
    if(address>=FLASH_SIZE){return 1U;}
    address&=~(uint32_t)(FLASH_SECTOR_SIZE-1U);
    if(flash_wait_ready(FLASH_PROGRAM_TIMEOUT)!=0U){return 1U;}
    if(flash_write_enable()!=0U){return 1U;}
    command[0]=FLASH_CMD_SECTOR_ERASE;
    flash_address_bytes(command,address);
    flash_cs_low();
    if(flash_transmit(command,4U)!=0U){
        flash_cs_high();
        return 1U;
    }
    flash_cs_high();
    return flash_wait_ready(FLASH_ERASE_TIMEOUT);
}

uint8_t Flash_write(uint32_t address,const uint8_t* data,uint32_t length){
    uint32_t sector_address;
    uint32_t sector_offset;
    uint32_t chunk;
    uint32_t i;
    uint8_t erase_needed;
    if(length==0U){return 0U;}
    if(data==NULL||address>=FLASH_SIZE||length>FLASH_SIZE-address){return 1U;}
    while(length!=0U){
        sector_address=address&~(uint32_t)(FLASH_SECTOR_SIZE-1U);
        sector_offset=address-sector_address;
        chunk=FLASH_SECTOR_SIZE-sector_offset;
        if(chunk>length){chunk=length;}
        if(Flash_read(sector_address,flash_sector_buffer,FLASH_SECTOR_SIZE)!=0U){return 1U;}
        erase_needed=0U;
        /*
         * NOR Flash 可直接把 1 写成 0；若目标数据需要把任意位从 0
         * 恢复为 1，则必须保存整扇区、擦除后再写回。
         */
        for(i=0U;i<chunk;i++){
            if((flash_sector_buffer[sector_offset+i]&data[i])!=data[i]){
                erase_needed=1U;
                break;
            }
        }
        if(erase_needed!=0U){
            for(i=0U;i<chunk;i++){flash_sector_buffer[sector_offset+i]=data[i];}
            if(Flash_erase_sector(sector_address)!=0U){return 1U;}
            for(i=0U;i<FLASH_SECTOR_SIZE;i+=FLASH_PAGE_SIZE){
                uint32_t j;
                uint8_t blank=1U;
                for(j=0U;j<FLASH_PAGE_SIZE;j++){
                    if(flash_sector_buffer[i+j]!=0xFFU){
                        blank=0U;
                        break;
                    }
                }
                /* 全 0xFF 页无需编程，可减少写入时间和器件磨损。 */
                if(blank==0U&&flash_program_pages(sector_address+i,&flash_sector_buffer[i],FLASH_PAGE_SIZE)!=0U){
                    return 1U;
                }
            }
        }
        else if(flash_program_pages(address,data,chunk)!=0U){
            return 1U;
        }
        address+=chunk;
        data+=chunk;
        length-=chunk;
    }
    return 0U;
}
