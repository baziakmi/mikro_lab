#include "platform.h"
#include "stm32f4xx_i2c.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_i2c.h"
#include "stm32f4xx_gpio.h"


void i2c_init() {
	GPIO_InitTypeDef GPIO_InitStructure;
	I2C_InitTypeDef I2C_InitStructure;
	
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
	
	 I2C1->CR1 |= I2C_CR1_SWRST;
    I2C1->CR1 &= ~I2C_CR1_SWRST;
	
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; 
    GPIO_Init(GPIOB, &GPIO_InitStructure);
  
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource8, GPIO_AF_I2C1);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource9, GPIO_AF_I2C1);
	
    I2C_InitStructure.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStructure.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStructure.I2C_OwnAddress1 = 0x00;
    I2C_InitStructure.I2C_Ack = I2C_Ack_Enable;
    I2C_InitStructure.I2C_ClockSpeed = 100000;
    I2C_InitStructure.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    
    I2C_Init(I2C1, &I2C_InitStructure);
    I2C_Cmd(I2C1, ENABLE);
}

void i2c_write(uint8_t address, uint8_t *buffer, int buff_len) {
    uint32_t timeout = 100000;
    
    while(I2C1->SR2 & I2C_SR2_BUSY) { if(--timeout == 0) return; }

    I2C1->CR1 |= I2C_CR1_START;
    timeout = 100000;
    while(!(I2C1->SR1 & I2C_SR1_SB)) { if(--timeout == 0) return; }

    I2C1->DR = address & 0xFE;
    timeout = 100000;
    while(!(I2C1->SR1 & I2C_SR1_ADDR)) { if(--timeout == 0) return; }
    (void)I2C1->SR1; (void)I2C1->SR2; // ?a?a??sµ?? t?? ADDR flag

    for(int i = 0; i < buff_len; i++) {
        I2C1->DR = buffer[i];
        timeout = 100000;
        while(!(I2C1->SR1 & I2C_SR1_TXE)) { if(--timeout == 0) return; }
    }
    
    timeout = 100000;
    while(!(I2C1->SR1 & I2C_SR1_BTF)) { if(--timeout == 0) return; }

    I2C1->CR1 |= I2C_CR1_STOP;
}

void i2c_read(uint8_t address, uint8_t *buffer, int buff_len) {
    uint32_t timeout = 100000;
    if (buff_len <= 0) return;

    while(I2C1->SR2 & I2C_SR2_BUSY) { if(--timeout == 0) return; }

    I2C1->CR1 |= I2C_CR1_START;
    timeout = 100000;
    while(!(I2C1->SR1 & I2C_SR1_SB)) { if(--timeout == 0) return; }

    I2C1->DR = address | 0x01;
    timeout = 100000;
    while(!(I2C1->SR1 & I2C_SR1_ADDR)) { if(--timeout == 0) return; }

    if(buff_len == 1) {
        I2C1->CR1 &= ~I2C_CR1_ACK;       
        __disable_irq();
        (void)I2C1->SR1; (void)I2C1->SR2; 
        I2C1->CR1 |= I2C_CR1_STOP;       
        __enable_irq();
        
        timeout = 100000;
        while(!(I2C1->SR1 & I2C_SR1_RXNE)) { if(--timeout == 0) return; }
        buffer[0] = I2C1->DR;             
    } 
    else {
        (void)I2C1->SR1; (void)I2C1->SR2; // ?a?a?????µe t? ADDR flag
        
        for(int i = 0; i < buff_len; i++) {
            if(i == buff_len - 1) {
                I2C1->CR1 &= ~I2C_CR1_ACK;
                I2C1->CR1 |= I2C_CR1_STOP; 
            }
            timeout = 100000;
            while(!(I2C1->SR1 & I2C_SR1_RXNE)) { if(--timeout == 0) return; }
            buffer[i] = I2C1->DR;
        }
    }

    I2C1->CR1 |= I2C_CR1_ACK;
}