#include "lcd.h"
#include "flash.h"

#define LCD_CMD_SWRESET         0x01U
#define LCD_CMD_SLPIN           0x10U
#define LCD_CMD_SLPOUT          0x11U
#define LCD_CMD_DISPOFF         0x28U
#define LCD_CMD_DISPON          0x29U
#define LCD_CMD_CASET           0x2AU
#define LCD_CMD_PASET           0x2BU
#define LCD_CMD_RAMWR           0x2CU
#define LCD_CMD_MADCTL          0x36U
#define LCD_CMD_PIXFMT          0x3AU
#define LCD_SPI_TIMEOUT         100U
#define LCD_DMA_TIMEOUT         1000U
#define LCD_TRANSFER_BUFFER_SIZE 2048U
#define LCD_TRANSFER_PIXELS     (LCD_TRANSFER_BUFFER_SIZE/2U)
#define LCD_DMA_MIN_BYTES       32U
#define LCD_LANDSCAPE_MODE      2U
#define LCD_GBK_SLOT_COUNT      23940UL
#define LCD_GBK16_ADDRESS       0x00000000UL
#define LCD_GBK24_ADDRESS       0x000BC000UL
/* 商家例程默认使用 Mode 3；实物验证时可在 lcd.h 中切换为 Mode 0。 */
#if LCD_SPI_MODE!=0U&&LCD_SPI_MODE!=3U
#error "LCD_SPI_MODE only supports 0 or 3"
#endif
#if LCD_LANDSCAPE_MODE==2U
#define LCD_MADCTL_VALUE        0x78U
#else
#define LCD_MADCTL_VALUE        0xA8U
#endif

static SPI_HandleTypeDef lcd_spi;
static DMA_HandleTypeDef lcd_dma;
static TIM_HandleTypeDef lcd_backlight_timer;
static uint8_t lcd_transfer_buffer[LCD_TRANSFER_BUFFER_SIZE];
static uint8_t lcd_gbk_glyph_buffer[72U];
static const uint8_t lcd_blank_glyph[128U]={0U};
static uint8_t lcd_backlight_percent;
static uint8_t lcd_initialized;
static uint8_t lcd_sleeping;
static uint32_t lcd_spi_speed_hz;

static void lcd_cs_low(void){
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port,LCD_CS_Pin,GPIO_PIN_RESET);
}

static void lcd_cs_high(void){
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port,LCD_CS_Pin,GPIO_PIN_SET);
}

static uint8_t lcd_write_command_selected(uint8_t command){
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port,LCD_DC_Pin,GPIO_PIN_RESET);
    if(HAL_SPI_Transmit(&lcd_spi,&command,1U,LCD_SPI_TIMEOUT)!=HAL_OK){
        return 1U;
    }
    return 0U;
}

static uint8_t lcd_write_data_selected(const uint8_t* data,uint16_t length){
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port,LCD_DC_Pin,GPIO_PIN_SET);
    if(HAL_SPI_Transmit(&lcd_spi,(uint8_t*)data,length,LCD_SPI_TIMEOUT)!=HAL_OK){
        return 1U;
    }
    return 0U;
}

static uint8_t lcd_write_command(uint8_t command){
    uint8_t result;
    lcd_cs_low();
    result=lcd_write_command_selected(command);
    lcd_cs_high();
    return result;
}

static uint8_t lcd_write_data(const uint8_t* data,uint16_t length){
    uint8_t result;
    lcd_cs_low();
    result=lcd_write_data_selected(data,length);
    lcd_cs_high();
    return result;
}

/*
 * 发送一个 DMA 数据块。调用者负责在整块像素流开始前拉低 CS，
 * 并在全部分块完成或出错后释放 CS。
 */
static uint8_t lcd_dma_transmit(const uint8_t* data,uint16_t length){
    uint32_t start;
    if(length==0U){return 0U;}
    if(HAL_SPI_Transmit_DMA(&lcd_spi,(uint8_t*)data,length)!=HAL_OK){
        return 1U;
    }
    start=HAL_GetTick();
    while(HAL_SPI_GetState(&lcd_spi)!=HAL_SPI_STATE_READY){
        if((uint32_t)(HAL_GetTick()-start)>=LCD_DMA_TIMEOUT){
            HAL_SPI_Abort(&lcd_spi);
            return 1U;
        }
    }
    return HAL_SPI_GetError(&lcd_spi)==HAL_SPI_ERROR_NONE?0U:1U;
}

static uint8_t lcd_stream_transmit(const uint8_t* data,uint16_t length){
    if(length==0U){return 0U;}
    if(length<LCD_DMA_MIN_BYTES){
        return HAL_SPI_Transmit(&lcd_spi,(uint8_t*)data,length,LCD_SPI_TIMEOUT)==HAL_OK?0U:1U;
    }
    return lcd_dma_transmit(data,length);
}

static void lcd_stream_end(void){
    lcd_cs_high();
}

/* 设置地址窗口后保持 CS 为低，调用者发送完全部像素后必须调用 lcd_stream_end。 */
static uint8_t lcd_stream_window_begin(uint16_t x,uint16_t y,uint16_t width,uint16_t height){
    uint8_t data[4];
    uint16_t end;
    if(width==0U||height==0U||x>=LCD_WIDTH||y>=LCD_HEIGHT||
       width>LCD_WIDTH-x||height>LCD_HEIGHT-y){return 1U;}
    lcd_cs_low();
    end=(uint16_t)(x+width-1U);
    data[0]=(uint8_t)(x>>8);
    data[1]=(uint8_t)x;
    data[2]=(uint8_t)(end>>8);
    data[3]=(uint8_t)end;
    if(lcd_write_command_selected(LCD_CMD_CASET)!=0U||lcd_write_data_selected(data,4U)!=0U){
        lcd_stream_end();
        return 1U;
    }
    end=(uint16_t)(y+height-1U);
    data[0]=(uint8_t)(y>>8);
    data[1]=(uint8_t)y;
    data[2]=(uint8_t)(end>>8);
    data[3]=(uint8_t)end;
    if(lcd_write_command_selected(LCD_CMD_PASET)!=0U||lcd_write_data_selected(data,4U)!=0U||
       lcd_write_command_selected(LCD_CMD_RAMWR)!=0U){
        lcd_stream_end();
        return 1U;
    }
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port,LCD_DC_Pin,GPIO_PIN_SET);
    return 0U;
}

static uint32_t lcd_spi_prescaler_divider(uint32_t prescaler){
    switch(prescaler){
        case SPI_BAUDRATEPRESCALER_2:return 2U;
        case SPI_BAUDRATEPRESCALER_4:return 4U;
        case SPI_BAUDRATEPRESCALER_8:return 8U;
        case SPI_BAUDRATEPRESCALER_16:return 16U;
        case SPI_BAUDRATEPRESCALER_32:return 32U;
        case SPI_BAUDRATEPRESCALER_64:return 64U;
        case SPI_BAUDRATEPRESCALER_128:return 128U;
        default:return 256U;
    }
}

static uint8_t lcd_spi_speed_config(uint32_t speed_hz,uint32_t* prescaler){
    if(prescaler==NULL){return 1U;}
    switch(speed_hz){
        case LCD_SPI_SPEED_3_125MHZ:*prescaler=SPI_BAUDRATEPRESCALER_32;return 0U;
        case LCD_SPI_SPEED_6_25MHZ:*prescaler=SPI_BAUDRATEPRESCALER_16;return 0U;
        case LCD_SPI_SPEED_12_5MHZ:*prescaler=SPI_BAUDRATEPRESCALER_8;return 0U;
        case LCD_SPI_SPEED_25MHZ:*prescaler=SPI_BAUDRATEPRESCALER_4;return 0U;
        case LCD_SPI_SPEED_50MHZ:*prescaler=SPI_BAUDRATEPRESCALER_2;return 0U;
        default:return 1U;
    }
}

static uint32_t lcd_timer_clock(void){
    RCC_ClkInitTypeDef clock_config;
    uint32_t flash_latency;
    uint32_t clock=HAL_RCC_GetPCLK2Freq();
    HAL_RCC_GetClockConfig(&clock_config,&flash_latency);
    if(clock_config.APB2CLKDivider!=RCC_HCLK_DIV1){clock*=2U;}
    return clock;
}

static void lcd_backlight_init(void){
    GPIO_InitTypeDef gpio={0};
    TIM_OC_InitTypeDef pwm={0};
    uint32_t timer_clock=lcd_timer_clock();
    __HAL_RCC_TIM1_CLK_ENABLE();
    gpio.Pin=LCD_BL_PWM_Pin;
    gpio.Mode=GPIO_MODE_AF_PP;
    gpio.Pull=GPIO_NOPULL;
    gpio.Speed=GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate=GPIO_AF1_TIM1;
    HAL_GPIO_Init(LCD_BL_PWM_GPIO_Port,&gpio);
    lcd_backlight_timer.Instance=TIM1;
    lcd_backlight_timer.Init.Prescaler=(timer_clock/20000000U)-1U;
    lcd_backlight_timer.Init.CounterMode=TIM_COUNTERMODE_UP;
    lcd_backlight_timer.Init.Period=999U;
    lcd_backlight_timer.Init.ClockDivision=TIM_CLOCKDIVISION_DIV1;
    lcd_backlight_timer.Init.RepetitionCounter=0U;
    lcd_backlight_timer.Init.AutoReloadPreload=TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&lcd_backlight_timer);
    pwm.OCMode=TIM_OCMODE_PWM1;
    pwm.Pulse=0U;
    pwm.OCPolarity=TIM_OCPOLARITY_HIGH;
    pwm.OCFastMode=TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&lcd_backlight_timer,&pwm,TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&lcd_backlight_timer,TIM_CHANNEL_1);
}

/* 使用商家针对当前 3.2 英寸模组提供的 ILI9341 初始化参数。 */
static uint8_t lcd_init_sequence(void){
    static const uint8_t power_b[]={0x00U,0xC1U,0x30U};
    static const uint8_t power_seq[]={0x64U,0x03U,0x12U,0x81U};
    static const uint8_t driver_a[]={0x85U,0x00U,0x79U};
    static const uint8_t power_a[]={0x39U,0x2CU,0x00U,0x34U,0x02U};
    static const uint8_t driver_b[]={0x00U,0x00U};
    static const uint8_t frame[]={0x00U,0x12U};
    static const uint8_t display[]={0x0AU,0xA2U};
    static const uint8_t gamma_pos[]={0x0FU,0x22U,0x1CU,0x1BU,0x08U,0x0FU,0x48U,0xB8U,0x34U,0x05U,0x0CU,0x09U,0x0FU,0x07U,0x00U};
    static const uint8_t gamma_neg[]={0x00U,0x23U,0x24U,0x07U,0x10U,0x07U,0x38U,0x47U,0x4BU,0x0AU,0x13U,0x06U,0x30U,0x38U,0x0FU};
    uint8_t value;
#define LCD_SEND(cmd,data,length) do{if(lcd_write_command(cmd)!=0U||lcd_write_data(data,length)!=0U){return 1U;}}while(0)
    if(lcd_write_command(LCD_CMD_SLPOUT)!=0U){return 1U;}
    HAL_Delay(120U);
    LCD_SEND(0xCFU,power_b,3U);
    LCD_SEND(0xEDU,power_seq,4U);
    LCD_SEND(0xE8U,driver_a,3U);
    LCD_SEND(0xCBU,power_a,5U);
    value=0x20U;LCD_SEND(0xF7U,&value,1U);
    LCD_SEND(0xEAU,driver_b,2U);
    value=0x1DU;LCD_SEND(0xC0U,&value,1U);
    value=0x12U;LCD_SEND(0xC1U,&value,1U);
    {uint8_t vcom[]={0x33U,0x3FU};LCD_SEND(0xC5U,vcom,2U);}
    value=0x92U;LCD_SEND(0xC7U,&value,1U);
    value=0x55U;LCD_SEND(LCD_CMD_PIXFMT,&value,1U);
    value=LCD_MADCTL_VALUE;LCD_SEND(LCD_CMD_MADCTL,&value,1U);
    LCD_SEND(0xB1U,frame,2U);
    LCD_SEND(0xB6U,display,2U);
    value=0x02U;LCD_SEND(0x44U,&value,1U);
    value=0x00U;LCD_SEND(0xF2U,&value,1U);
    value=0x01U;LCD_SEND(0x26U,&value,1U);
    LCD_SEND(0xE0U,gamma_pos,15U);
    LCD_SEND(0xE1U,gamma_neg,15U);
#undef LCD_SEND
    if(lcd_write_command(LCD_CMD_DISPON)!=0U){return 1U;}
    HAL_Delay(20U);
    return 0U;
}

/*
 * 字库按列保存，每列由高位到低位排列若干字节；
 * 12/16/24/32 点阵分别对应 6/8/12/16 像素宽；GBK 汉字在 12、32 点阵时留空占位。
 */
static const uint8_t* lcd_ascii_font_data(char character,uint8_t font_size,uint8_t* width,uint8_t* height){
    uint8_t index;
    if(character<' '||character>'~'){character='?';}
    index=(uint8_t)character-(uint8_t)' ';
    switch(font_size){
        case 12U:*width=6U;*height=12U;return asc_1206[index];
        case 16U:*width=8U;*height=16U;return asc_1608[index];
        case 24U:*width=12U;*height=24U;return asc_2412[index];
        case 32U:*width=16U;*height=32U;return asc_3216[index];
        default:return NULL;
    }
}

typedef struct{
    const uint8_t* data;
    uint8_t width;
    uint8_t height;
    uint8_t bytes_per_column;
    uint8_t consumed;
} LcdGlyph;

static uint8_t lcd_gbk_slot(uint8_t lead,uint8_t trail,uint32_t* slot){
    uint32_t trail_index;
    if(slot==NULL||lead<0x81U||lead>0xFEU||trail<0x40U||trail==0x7FU||trail>0xFEU){return 1U;}
    trail_index=trail<0x7FU?(uint32_t)trail-0x40U:(uint32_t)trail-0x80U+63U;
    *slot=((uint32_t)lead-0x81U)*190UL+trail_index;
    return *slot<LCD_GBK_SLOT_COUNT?0U:1U;
}

static uint8_t lcd_load_glyph(const uint8_t* text,uint8_t font_size,LcdGlyph* glyph){
    const uint8_t* ascii_font;
    uint8_t ascii_width;
    uint8_t ascii_height;
    uint32_t slot;
    uint32_t address;
    if(text==NULL||glyph==NULL||*text==0U){return 1U;}
    if(*text<0x80U){
        ascii_font=lcd_ascii_font_data((char)*text,font_size,&ascii_width,&ascii_height);
        if(ascii_font==NULL){return 1U;}
        glyph->data=ascii_font;
        glyph->width=ascii_width;
        glyph->height=ascii_height;
        glyph->bytes_per_column=(uint8_t)((ascii_height+7U)/8U);
        glyph->consumed=1U;
        return 0U;
    }
    if(text[1]==0U||lcd_gbk_slot(*text,text[1],&slot)!=0U){
        ascii_font=lcd_ascii_font_data('?',font_size,&ascii_width,&ascii_height);
        if(ascii_font==NULL){return 1U;}
        glyph->data=ascii_font;
        glyph->width=ascii_width;
        glyph->height=ascii_height;
        glyph->bytes_per_column=(uint8_t)((ascii_height+7U)/8U);
        glyph->consumed=1U;
        return 0U;
    }
    glyph->width=font_size;
    glyph->height=font_size;
    glyph->bytes_per_column=(uint8_t)((glyph->height+7U)/8U);
    glyph->consumed=2U;
    switch(font_size){
        case 12U:
        case 32U:
            glyph->data=lcd_blank_glyph;
            return 0U;
        case 16U:address=LCD_GBK16_ADDRESS;break;
        case 24U:address=LCD_GBK24_ADDRESS;break;
        default:return 1U;
    }
    if(Flash_read(address+slot*(uint32_t)(glyph->width*((glyph->height+7U)/8U)),
                  lcd_gbk_glyph_buffer,
                  (uint32_t)glyph->width*((glyph->height+7U)/8U))!=0U){return 1U;}
    glyph->data=lcd_gbk_glyph_buffer;
    return 0U;
}

uint8_t Lcd_init(void){
    GPIO_InitTypeDef gpio={0};
    lcd_initialized=0U;
    lcd_sleeping=0U;
    lcd_spi_speed_hz=0U;
    HAL_GPIO_WritePin(GPIOA,LCD_CS_Pin|LCD_RST_Pin,GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOA,LCD_DC_Pin,GPIO_PIN_RESET);
    gpio.Pin=LCD_CS_Pin|LCD_DC_Pin|LCD_RST_Pin;
    gpio.Mode=GPIO_MODE_OUTPUT_PP;
    gpio.Pull=GPIO_NOPULL;
    gpio.Speed=GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA,&gpio);
    gpio.Pin=LCD_SCK_Pin|LCD_MOSI_Pin;
    gpio.Mode=GPIO_MODE_AF_PP;
    gpio.Pull=GPIO_NOPULL;
    gpio.Speed=GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate=GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA,&gpio);
    lcd_dma.Instance=DMA2_Stream3;
    lcd_dma.Init.Channel=DMA_CHANNEL_3;
    lcd_dma.Init.Direction=DMA_MEMORY_TO_PERIPH;
    lcd_dma.Init.PeriphInc=DMA_PINC_DISABLE;
    lcd_dma.Init.MemInc=DMA_MINC_ENABLE;
    lcd_dma.Init.PeriphDataAlignment=DMA_PDATAALIGN_BYTE;
    lcd_dma.Init.MemDataAlignment=DMA_MDATAALIGN_BYTE;
    lcd_dma.Init.Mode=DMA_NORMAL;
    lcd_dma.Init.Priority=DMA_PRIORITY_HIGH;
    lcd_dma.Init.FIFOMode=DMA_FIFOMODE_DISABLE;
    if(HAL_DMA_Init(&lcd_dma)!=HAL_OK){return 1U;}
    lcd_spi.Instance=SPI1;
    lcd_spi.Init.Mode=SPI_MODE_MASTER;
    lcd_spi.Init.Direction=SPI_DIRECTION_2LINES;
    lcd_spi.Init.DataSize=SPI_DATASIZE_8BIT;
#if LCD_SPI_MODE==3U
    lcd_spi.Init.CLKPolarity=SPI_POLARITY_HIGH;
    lcd_spi.Init.CLKPhase=SPI_PHASE_2EDGE;
#else
    lcd_spi.Init.CLKPolarity=SPI_POLARITY_LOW;
    lcd_spi.Init.CLKPhase=SPI_PHASE_1EDGE;
#endif
    lcd_spi.Init.NSS=SPI_NSS_SOFT;
    lcd_spi.Init.BaudRatePrescaler=LCD_SPI_PRESCALER;
    lcd_spi.Init.FirstBit=SPI_FIRSTBIT_MSB;
    lcd_spi.Init.TIMode=SPI_TIMODE_DISABLE;
    lcd_spi.Init.CRCCalculation=SPI_CRCCALCULATION_DISABLE;
    lcd_spi.Init.CRCPolynomial=7U;
    if(HAL_SPI_Init(&lcd_spi)!=HAL_OK){return 1U;}
    lcd_spi_speed_hz=HAL_RCC_GetPCLK2Freq()/lcd_spi_prescaler_divider(lcd_spi.Init.BaudRatePrescaler);
    __HAL_LINKDMA(&lcd_spi,hdmatx,lcd_dma);
    HAL_NVIC_SetPriority(DMA2_Stream3_IRQn,5U,0U);
    HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
    lcd_backlight_init();
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port,LCD_RST_Pin,GPIO_PIN_RESET);
    HAL_Delay(20U);
    HAL_GPIO_WritePin(LCD_RST_GPIO_Port,LCD_RST_Pin,GPIO_PIN_SET);
    HAL_Delay(120U);
    if(lcd_write_command(LCD_CMD_SWRESET)!=0U){return 1U;}
    HAL_Delay(10U);
    if(lcd_init_sequence()!=0U){return 1U;}
    lcd_backlight_percent=0U;
    lcd_sleeping=0U;
    lcd_initialized=1U;
    return 0U;
}

uint8_t Lcd_set_spi_speed(uint32_t speed_hz){
    uint32_t prescaler;
    uint32_t start;
    if(lcd_initialized==0U||lcd_spi_speed_config(speed_hz,&prescaler)!=0U){return 1U;}
    if(lcd_spi_speed_hz==speed_hz){return 0U;}
    start=HAL_GetTick();
    while(HAL_SPI_GetState(&lcd_spi)!=HAL_SPI_STATE_READY||
          __HAL_SPI_GET_FLAG(&lcd_spi,SPI_FLAG_BSY)!=RESET){
        if((uint32_t)(HAL_GetTick()-start)>=LCD_SPI_TIMEOUT){return 1U;}
    }
    lcd_cs_high();
    __HAL_SPI_DISABLE(&lcd_spi);
    MODIFY_REG(lcd_spi.Instance->CR1,SPI_CR1_BR,prescaler);
    lcd_spi.Init.BaudRatePrescaler=prescaler;
    __HAL_SPI_ENABLE(&lcd_spi);
    lcd_spi_speed_hz=HAL_RCC_GetPCLK2Freq()/lcd_spi_prescaler_divider(prescaler);
    return lcd_spi_speed_hz==speed_hz?0U:1U;
}

uint32_t Lcd_get_spi_speed(void){
    return lcd_spi_speed_hz;
}

void Lcd_set_backlight(uint8_t percent){
    if(percent>100U){percent=100U;}
    lcd_backlight_percent=percent;
    if(lcd_sleeping==0U){
        __HAL_TIM_SET_COMPARE(&lcd_backlight_timer,TIM_CHANNEL_1,(1000U*percent)/100U);
    }
}

uint8_t Lcd_clear(uint16_t color){
    return Lcd_fill_rect(0U,0U,LCD_WIDTH,LCD_HEIGHT,color);
}

uint8_t Lcd_fill_rect(uint16_t x,uint16_t y,uint16_t width,uint16_t height,uint16_t color){
    uint16_t draw_width;
    uint16_t draw_height;
    uint16_t buffer_pixels;
    uint16_t i;
    uint32_t remaining;
    if(width==0U||height==0U){return 0U;}
    if(x>=LCD_WIDTH||y>=LCD_HEIGHT){return 1U;}
    draw_width=width;
    draw_height=height;
    if(draw_width>LCD_WIDTH-x){draw_width=(uint16_t)(LCD_WIDTH-x);}
    if(draw_height>LCD_HEIGHT-y){draw_height=(uint16_t)(LCD_HEIGHT-y);}
    remaining=(uint32_t)draw_width*draw_height;
    buffer_pixels=remaining>LCD_TRANSFER_PIXELS?LCD_TRANSFER_PIXELS:(uint16_t)remaining;
    for(i=0U;i<buffer_pixels;i++){
        lcd_transfer_buffer[i*2U]=(uint8_t)(color>>8);
        lcd_transfer_buffer[i*2U+1U]=(uint8_t)color;
    }
    if(lcd_stream_window_begin(x,y,draw_width,draw_height)!=0U){return 1U;}
    while(remaining!=0U){
        uint16_t chunk=remaining>buffer_pixels?buffer_pixels:(uint16_t)remaining;
        if(lcd_stream_transmit(lcd_transfer_buffer,(uint16_t)(chunk*2U))!=0U){
            lcd_stream_end();
            return 1U;
        }
        remaining-=chunk;
    }
    lcd_stream_end();
    return 0U;
}

uint8_t Lcd_draw_pixel(uint16_t x,uint16_t y,uint16_t color){
    uint8_t data[2];
    if(x>=LCD_WIDTH||y>=LCD_HEIGHT){return 1U;}
    data[0]=(uint8_t)(color>>8);
    data[1]=(uint8_t)color;
    if(lcd_stream_window_begin(x,y,1U,1U)!=0U){return 1U;}
    if(lcd_stream_transmit(data,2U)!=0U){lcd_stream_end();return 1U;}
    lcd_stream_end();
    return 0U;
}

static uint8_t lcd_draw_horizontal_span_clipped(int32_t x0,int32_t x1,int32_t y,uint16_t color){
    int32_t start=x0<x1?x0:x1;
    int32_t end=x0>x1?x0:x1;
    if(y<0||y>=(int32_t)LCD_HEIGHT||end<0||start>=(int32_t)LCD_WIDTH){return 0U;}
    if(start<0){start=0;}
    if(end>=(int32_t)LCD_WIDTH){end=(int32_t)LCD_WIDTH-1;}
    return Lcd_fill_rect((uint16_t)start,(uint16_t)y,(uint16_t)(end-start+1),1U,color);
}

uint8_t Lcd_draw_line(int16_t x0,int16_t y0,int16_t x1,int16_t y1,uint16_t color){
    int32_t current_x=x0;
    int32_t current_y=y0;
    int32_t target_x=x1;
    int32_t target_y=y1;
    int32_t dx=target_x>current_x?target_x-current_x:current_x-target_x;
    int32_t sx=current_x<target_x?1:-1;
    int32_t dy=target_y>current_y?current_y-target_y:target_y-current_y;
    int32_t sy=current_y<target_y?1:-1;
    int32_t error=dx+dy;
    int32_t run_y=current_y;
    int32_t run_start=current_x;
    int32_t run_end=current_x;
    if(current_y==target_y){return lcd_draw_horizontal_span_clipped(current_x,target_x,current_y,color);}
    if(current_x==target_x){
        int32_t start=current_y<target_y?current_y:target_y;
        int32_t end=current_y>target_y?current_y:target_y;
        if(current_x<0||current_x>=(int32_t)LCD_WIDTH||end<0||start>=(int32_t)LCD_HEIGHT){return 0U;}
        if(start<0){start=0;}
        if(end>=(int32_t)LCD_HEIGHT){end=(int32_t)LCD_HEIGHT-1;}
        return Lcd_fill_rect((uint16_t)current_x,(uint16_t)start,1U,(uint16_t)(end-start+1),color);
    }
    while(current_x!=target_x||current_y!=target_y){
        int32_t next_x=current_x;
        int32_t next_y=current_y;
        int32_t twice=error*2;
        if(twice>=dy){error+=dy;next_x+=sx;}
        if(twice<=dx){error+=dx;next_y+=sy;}
        if(next_y==run_y){
            run_end=next_x;
        }
        else{
            if(lcd_draw_horizontal_span_clipped(run_start,run_end,run_y,color)!=0U){return 1U;}
            run_y=next_y;
            run_start=next_x;
            run_end=next_x;
        }
        current_x=next_x;
        current_y=next_y;
    }
    return lcd_draw_horizontal_span_clipped(run_start,run_end,run_y,color);
}

uint8_t Lcd_draw_rect(uint16_t x,uint16_t y,uint16_t width,uint16_t height,uint16_t color){
    if(width==0U||height==0U){return 0U;}
    if(x>=LCD_WIDTH||y>=LCD_HEIGHT){return 1U;}
    if(width>LCD_WIDTH-x){width=(uint16_t)(LCD_WIDTH-x);}
    if(height>LCD_HEIGHT-y){height=(uint16_t)(LCD_HEIGHT-y);}
    if(Lcd_fill_rect(x,y,width,1U,color)!=0U){return 1U;}
    if(height>1U&&Lcd_fill_rect(x,(uint16_t)(y+height-1U),width,1U,color)!=0U){return 1U;}
    if(height>2U&&Lcd_fill_rect(x,(uint16_t)(y+1U),1U,(uint16_t)(height-2U),color)!=0U){return 1U;}
    if(width>1U&&height>2U&&Lcd_fill_rect((uint16_t)(x+width-1U),(uint16_t)(y+1U),1U,(uint16_t)(height-2U),color)!=0U){return 1U;}
    return 0U;
}

/* 圆周点超出屏幕时直接忽略，使靠近边缘的圆能够安全裁剪。 */
static uint8_t lcd_draw_pixel_clipped(int32_t x,int32_t y,uint16_t color){
    if(x<0||x>=(int32_t)LCD_WIDTH||y<0||y>=(int32_t)LCD_HEIGHT){return 0U;}
    return Lcd_draw_pixel((uint16_t)x,(uint16_t)y,color);
}

uint8_t Lcd_draw_circle(int16_t x0,int16_t y0,uint16_t radius,uint16_t color){
    int32_t x=radius;
    int32_t y=0;
    int32_t error=1-x;
    if(radius==0U){return lcd_draw_pixel_clipped(x0,y0,color);}
    while(x>=y){
        if(y==0){
            if(lcd_draw_pixel_clipped((int32_t)x0+x,y0,color)!=0U){return 1U;}
            if(lcd_draw_pixel_clipped((int32_t)x0-x,y0,color)!=0U){return 1U;}
            if(lcd_draw_pixel_clipped(x0,(int32_t)y0+x,color)!=0U){return 1U;}
            if(lcd_draw_pixel_clipped(x0,(int32_t)y0-x,color)!=0U){return 1U;}
        }
        else if(x==y){
            if(lcd_draw_pixel_clipped((int32_t)x0+x,(int32_t)y0+y,color)!=0U){return 1U;}
            if(lcd_draw_pixel_clipped((int32_t)x0-x,(int32_t)y0+y,color)!=0U){return 1U;}
            if(lcd_draw_pixel_clipped((int32_t)x0-x,(int32_t)y0-y,color)!=0U){return 1U;}
            if(lcd_draw_pixel_clipped((int32_t)x0+x,(int32_t)y0-y,color)!=0U){return 1U;}
        }
        else{
            if(lcd_draw_pixel_clipped((int32_t)x0+x,(int32_t)y0+y,color)!=0U){return 1U;}
            if(lcd_draw_pixel_clipped((int32_t)x0+y,(int32_t)y0+x,color)!=0U){return 1U;}
            if(lcd_draw_pixel_clipped((int32_t)x0-y,(int32_t)y0+x,color)!=0U){return 1U;}
            if(lcd_draw_pixel_clipped((int32_t)x0-x,(int32_t)y0+y,color)!=0U){return 1U;}
            if(lcd_draw_pixel_clipped((int32_t)x0-x,(int32_t)y0-y,color)!=0U){return 1U;}
            if(lcd_draw_pixel_clipped((int32_t)x0-y,(int32_t)y0-x,color)!=0U){return 1U;}
            if(lcd_draw_pixel_clipped((int32_t)x0+y,(int32_t)y0-x,color)!=0U){return 1U;}
            if(lcd_draw_pixel_clipped((int32_t)x0+x,(int32_t)y0-y,color)!=0U){return 1U;}
        }
        y++;
        if(error<0){
            error+=2*y+1;
        }
        else{
            x--;
            error+=2*(y-x)+1;
        }
    }
    return 0U;
}

uint8_t Lcd_draw_image(uint16_t x,uint16_t y,uint16_t width,uint16_t height,const uint16_t* image){
    uint16_t source_width=width;
    uint16_t draw_width;
    uint16_t draw_height;
    uint16_t row;
    uint16_t col;
    uint16_t buffered=0U;
    if(width==0U||height==0U){return 0U;}
    if(image==NULL||x>=LCD_WIDTH||y>=LCD_HEIGHT){return 1U;}
    draw_width=width;
    draw_height=height;
    if(draw_width>LCD_WIDTH-x){draw_width=(uint16_t)(LCD_WIDTH-x);}
    if(draw_height>LCD_HEIGHT-y){draw_height=(uint16_t)(LCD_HEIGHT-y);}
    if(lcd_stream_window_begin(x,y,draw_width,draw_height)!=0U){return 1U;}
    for(row=0U;row<draw_height;row++){
        for(col=0U;col<draw_width;col++){
            uint16_t color=image[(uint32_t)row*source_width+col];
            lcd_transfer_buffer[buffered*2U]=(uint8_t)(color>>8);
            lcd_transfer_buffer[buffered*2U+1U]=(uint8_t)color;
            buffered++;
            if(buffered==LCD_TRANSFER_PIXELS){
                if(lcd_stream_transmit(lcd_transfer_buffer,LCD_TRANSFER_BUFFER_SIZE)!=0U){
                    lcd_stream_end();
                    return 1U;
                }
                buffered=0U;
            }
        }
    }
    if(buffered!=0U&&lcd_stream_transmit(lcd_transfer_buffer,(uint16_t)(buffered*2U))!=0U){
        lcd_stream_end();
        return 1U;
    }
    lcd_stream_end();
    return 0U;
}

uint8_t Lcd_draw_image_bytes(uint16_t x,uint16_t y,uint16_t width,uint16_t height,const uint8_t* image){
    uint16_t source_width=width;
    uint16_t draw_width;
    uint16_t draw_height;
    uint16_t row;
    if(width==0U||height==0U){return 0U;}
    if(image==NULL||x>=LCD_WIDTH||y>=LCD_HEIGHT){return 1U;}
    draw_width=width;
    draw_height=height;
    if(draw_width>LCD_WIDTH-x){draw_width=(uint16_t)(LCD_WIDTH-x);}
    if(draw_height>LCD_HEIGHT-y){draw_height=(uint16_t)(LCD_HEIGHT-y);}
    if(lcd_stream_window_begin(x,y,draw_width,draw_height)!=0U){return 1U;}
    if(draw_width==source_width){
        uint32_t remaining=(uint32_t)draw_width*draw_height*2U;
        uint32_t offset=0U;
        while(remaining!=0U){
            uint16_t chunk=remaining>65534UL?65534U:(uint16_t)remaining;
            if(lcd_stream_transmit(&image[offset],chunk)!=0U){lcd_stream_end();return 1U;}
            remaining-=chunk;
            offset+=chunk;
        }
    }
    else{
        for(row=0U;row<draw_height;row++){
            const uint8_t* row_data=&image[(uint32_t)row*source_width*2U];
            if(lcd_stream_transmit(row_data,(uint16_t)(draw_width*2U))!=0U){
                lcd_stream_end();
                return 1U;
            }
        }
    }
    lcd_stream_end();
    return 0U;
}

static uint8_t lcd_draw_glyph(uint16_t x,uint16_t y,const LcdGlyph* glyph,
                              uint16_t color,uint16_t background){
    uint8_t row;
    uint8_t col;
    uint16_t pixel;
    uint16_t index=0U;
    if(glyph==NULL||glyph->data==NULL||x+glyph->width>LCD_WIDTH||y+glyph->height>LCD_HEIGHT){return 1U;}
    for(row=0U;row<glyph->height;row++){
        for(col=0U;col<glyph->width;col++){
            pixel=(glyph->data[(uint16_t)col*glyph->bytes_per_column+row/8U]&
                   (0x80U>>(row&7U)))!=0U?color:background;
            lcd_transfer_buffer[index++]=(uint8_t)(pixel>>8);
            lcd_transfer_buffer[index++]=(uint8_t)pixel;
        }
    }
    if(lcd_stream_window_begin(x,y,glyph->width,glyph->height)!=0U){return 1U;}
    if(lcd_stream_transmit(lcd_transfer_buffer,index)!=0U){lcd_stream_end();return 1U;}
    lcd_stream_end();
    return 0U;
}

static uint8_t lcd_draw_glyph_transparent(uint16_t x,uint16_t y,const LcdGlyph* glyph,uint16_t color){
    uint8_t row;
    uint8_t col;
    uint8_t draw_width;
    uint8_t draw_height;
    if(glyph==NULL||glyph->data==NULL||x>=LCD_WIDTH||y>=LCD_HEIGHT){return 1U;}
    draw_width=glyph->width>LCD_WIDTH-x?(uint8_t)(LCD_WIDTH-x):glyph->width;
    draw_height=glyph->height>LCD_HEIGHT-y?(uint8_t)(LCD_HEIGHT-y):glyph->height;
    for(col=0U;col<draw_width;col++){
        lcd_transfer_buffer[col*2U]=(uint8_t)(color>>8);
        lcd_transfer_buffer[col*2U+1U]=(uint8_t)color;
    }
    for(row=0U;row<draw_height;row++){
        col=0U;
        while(col<draw_width){
            uint8_t run_start;
            while(col<draw_width&&
                  (glyph->data[(uint16_t)col*glyph->bytes_per_column+row/8U]&
                   (0x80U>>(row&7U)))==0U){col++;}
            run_start=col;
            while(col<draw_width&&
                  (glyph->data[(uint16_t)col*glyph->bytes_per_column+row/8U]&
                   (0x80U>>(row&7U)))!=0U){col++;}
            if(col>run_start){
                uint8_t run_length=(uint8_t)(col-run_start);
                if(lcd_stream_window_begin((uint16_t)(x+run_start),(uint16_t)(y+row),run_length,1U)!=0U){return 1U;}
                if(lcd_stream_transmit(lcd_transfer_buffer,(uint16_t)run_length*2U)!=0U){
                    lcd_stream_end();
                    return 1U;
                }
                lcd_stream_end();
            }
        }
    }
    return 0U;
}

static uint8_t lcd_draw_text_internal(uint16_t x,uint16_t y,const char* text,
                                      uint16_t color,uint16_t background,
                                      uint8_t font_size,uint8_t transparent){
    const uint8_t* cursor=(const uint8_t*)text;
    LcdGlyph glyph;
    if(text==NULL){return 1U;}
    while(*cursor!=0U){
        if(lcd_load_glyph(cursor,font_size,&glyph)!=0U){return 1U;}
        if(x+glyph.width>LCD_WIDTH){
            x=0U;
            y=(uint16_t)(y+glyph.height);
        }
        if(y+glyph.height>LCD_HEIGHT){return 1U;}
        if(transparent!=0U){
            if(lcd_draw_glyph_transparent(x,y,&glyph,color)!=0U){return 1U;}
        }else if(lcd_draw_glyph(x,y,&glyph,color,background)!=0U){
            return 1U;
        }
        x=(uint16_t)(x+glyph.width);
        cursor+=glyph.consumed;
    }
    return 0U;
}

uint8_t Lcd_draw_text(uint16_t x,uint16_t y,const char* text,uint16_t color,uint16_t background,uint8_t font_size){
    return lcd_draw_text_internal(x,y,text,color,background,font_size,0U);
}

uint8_t Lcd_draw_text_transparent(uint16_t x,uint16_t y,const char* text,uint16_t color,uint8_t font_size){
    return lcd_draw_text_internal(x,y,text,color,LCD_COLOR_BLACK,font_size,1U);
}

uint8_t Lcd_sleep(void){
    if(lcd_initialized==0U){return 1U;}
    if(lcd_sleeping!=0U){return 0U;}
    __HAL_TIM_SET_COMPARE(&lcd_backlight_timer,TIM_CHANNEL_1,0U);
    if(lcd_write_command(LCD_CMD_DISPOFF)!=0U){
        __HAL_TIM_SET_COMPARE(&lcd_backlight_timer,TIM_CHANNEL_1,(1000U*lcd_backlight_percent)/100U);
        return 1U;
    }
    HAL_Delay(10U);
    if(lcd_write_command(LCD_CMD_SLPIN)!=0U){
        lcd_write_command(LCD_CMD_DISPON);
        __HAL_TIM_SET_COMPARE(&lcd_backlight_timer,TIM_CHANNEL_1,(1000U*lcd_backlight_percent)/100U);
        return 1U;
    }
    HAL_Delay(120U);
    lcd_sleeping=1U;
    return 0U;
}

uint8_t Lcd_wake(void){
    if(lcd_initialized==0U){return 1U;}
    if(lcd_sleeping==0U){return 0U;}
    if(lcd_write_command(LCD_CMD_SLPOUT)!=0U){return 1U;}
    HAL_Delay(120U);
    if(lcd_write_command(LCD_CMD_DISPON)!=0U){return 1U;}
    HAL_Delay(20U);
    lcd_sleeping=0U;
    __HAL_TIM_SET_COMPARE(&lcd_backlight_timer,TIM_CHANNEL_1,(1000U*lcd_backlight_percent)/100U);
    return 0U;
}

void DMA2_Stream3_IRQHandler(void){
    HAL_DMA_IRQHandler(&lcd_dma);
}
