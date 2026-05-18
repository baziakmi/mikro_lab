#include "platform.h"
#include "uart.h"
#include "queue.h"
#include "timer.h"
#include "gpio.h"
#include <string.h>
#include <stdbool.h>
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_usart.h"

#define BAUD_RATE 9600
#define COUNT_RATE 1000 // 1ms

// global metablhtes
Queue rx_queue;
int current_digit_idx = 0;
uint32_t digit_start_tick = 0;
char active_command[128];
char digit_sequence[128];
int sequence_length = 0;

//global volatile
volatile int count = 0;
volatile uint32_t override_tick = 0;
volatile uint32_t start_time = 0;

//gloabal flags
volatile bool system_locked = false;
volatile bool override_requested = false;
volatile bool msg_lock_needed = false;
volatile bool msg_override_needed = false;
volatile bool is_executing = false;
volatile bool loop_enabled = false;

////////////////////////////////	ISR
//bale xarakthra sthn oura
void my_uart_rx_callback(uint8_t rx_char) {
    if (!queue_is_full(&rx_queue)) queue_enqueue(&rx_queue, rx_char);
}

//timer
void timer_callback_isr(void) {
    count++;
}

//otan paththei to koumpi ti flags allazoun
void emergency_callback(int status) {
    if (!system_locked) {
        system_locked = true;
        is_executing = false;
        msg_lock_needed = true;
    } else {
        override_requested = true;
        override_tick = count;
        msg_override_needed = true;
    }
}
///////////////////////////////////////

int main(void) {
    uint8_t temp_char;
    char command_buffer[128];
    int idx = 0;

    queue_init(&rx_queue, 128);

    // arxikopoihsh periferiakwn
    SystemCoreClockUpdate();
    uart_init(BAUD_RATE); // baud rate
    uart_set_rx_callback(my_uart_rx_callback);
    timer_init(COUNT_RATE);// count auksanete kathe COUNT_RATE us
    timer_set_callback(timer_callback_isr);
	
		//set koumpi led ws ouput
    gpio_set_mode(P_LED_R, Output);
    gpio_set(P_LED_R, 0); // arxikopoihsh se off

    // Button Init
    gpio_set_mode(P_SW, PullDown); //0 oso den to pataw
    gpio_set_callback(P_SW, emergency_callback);//ti trexw otan patietai
    gpio_set_trigger(P_SW, Rising);// otan pataw koumpi ISR

    // proteraiothtes
    NVIC_SetPriority(EXTI15_10_IRQn, 0);//upsilotero
    NVIC_EnableIRQ(EXTI15_10_IRQn);
    NVIC_SetPriority(SysTick_IRQn, 1);
    NVIC_SetPriority(USART2_IRQn, 2);//xamhlotero

    uart_enable();
    timer_enable();
    __enable_irq();

    uart_print("System Ready. Enter Profile:\r\n> ");

    while (1) {
        // ektupwsh analoga ta flags
        if (msg_lock_needed) {
            uart_print("\r\n[EMERGENCY STOP] System Locked!\r\n");
            msg_lock_needed = false;
        }
        if (msg_override_needed) {
            uart_print("\r\nOverride requested. Awaiting password (5s)...\r\n> ");
            msg_override_needed = false;
        }

        // An perasoun 4 deuterolepta agnow oura
        if (idx > 0 && (count - start_time >= 4000)) {
            idx = 0;
            uart_print("\r\nTimeout! Buffer Cleared.\r\n> ");
        }

        if (queue_dequeue(&rx_queue, &temp_char)) {
            start_time = count; // Reset 4s timeout

            if (system_locked) {
                uart_print("\r\n[ERROR] SYSTEM LOCKED\r\n> ");
                
                //an patithei to koumpi 2h fora
                if (override_requested) {
                    if (temp_char == '\r' || temp_char == '\n') {
                        command_buffer[idx] = '\0';
                        //elegxos an egrapsa UNLOCK
                        if (count - override_tick <= 5000 && strcmp(command_buffer, "UNLOCK") == 0) {
                            system_locked = false;
                            override_requested = false;
                            gpio_set(P_LED_R, 0);
                            uart_print("\r\nSystem Unlocked.\r\n> ");
                        } else {
                            uart_print("\r\nWrong Password or Timeout.\r\n> ");
                        }
                        idx = 0;
                    } else if (idx < 127) {
                        command_buffer[idx++] = (char)temp_char;
                    }
                }
            } else {
                //emfanish xarakthra sthn othonh
                uart_tx(temp_char);


                if (temp_char == '\r' || temp_char == '\n') {
                    command_buffer[idx] = '\0';
                    if (idx > 0) {
                        int len = strlen(command_buffer);
                        loop_enabled = false;
                        if (len > 0 && command_buffer[len-1] == '-') {
                            loop_enabled = true;
                            command_buffer[len-1] = '\0'; // afairw paula
                        }
                        // antigrafh se digit_sequence apo to buffer
                        strcpy(digit_sequence, command_buffer);
                        sequence_length = strlen(digit_sequence);
                        current_digit_idx = 0;
                        digit_start_tick = count;
                        is_executing = true;
                        uart_print("\r\n");
                        if (loop_enabled) {
                            uart_print("Executing (looping)...\r\n");
                        } else {
                            uart_print("Executing...\r\n");
                        }
                        uart_print("> ");
                    } else {
                        uart_print("\r\n> ");
                    }
                    idx = 0;
                } else if (idx < 127) {
                    command_buffer[idx++] = (char)temp_char;
                }
            }
        }

        // Blinking LED 
        if (is_executing && !system_locked) {
            // led blink gia 2s
            if (count - digit_start_tick >= 2000) {
                current_digit_idx++;
                digit_start_tick = count;
                
                //elegxos an ftasame sto telos string
                if (current_digit_idx >= sequence_length) {
                    //an teleutaio einai pabla sunexise mexri na exoume neo input
					if (loop_enabled) {
                        current_digit_idx = 0; 
                    } else {
                        is_executing = false;
                    }
                }
            }
            
            if (is_executing && current_digit_idx < sequence_length) {
                char c = digit_sequence[current_digit_idx];
                if (c >= '1' && c <= '9') {
                    //metatroph apo ASCII se noumera
                    int freq = c - '0';
										//pragmatikos xronos ekinhshs
										uint32_t elapsed = count - digit_start_tick;
										uint32_t period_ms = 1000;  // 1s se ms
										//mhdenizetai kathe 1/freq s, apofugh diaireshs dioti sto 3 dhmiourgei problhma
										uint32_t time_in_period = (elapsed * freq) % period_ms;
										uint32_t half_period = period_ms / (2 * freq);
										//half duty cycle
										gpio_set(P_LED_R, time_in_period < half_period);
                } else if(c == '0') {
									gpio_set(P_LED_R, 0); //an 0 krata kleisto
								} else {
                    // gia sigouria an oxi 0-9 stamata
                    is_executing = false;
                    uart_print("\r\nInvalid digit. Execution stopped.\r\n> ");
                    gpio_set(P_LED_R, 0);
                }
            } else if (!is_executing) {
                gpio_set(P_LED_R, 0);
            }
        } else if (system_locked) {
            gpio_set(P_LED_R, 1);  // otan kleidwnei susthma sunexws anameno
        } else {
            gpio_set(P_LED_R, 0);// alliws krata kleisto gia sigouria
        }

        __WFI();
    }
}