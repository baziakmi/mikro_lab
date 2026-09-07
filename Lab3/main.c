// FIX: ?fa??????a? ta duplicate includes (platform.h, uart.h, delay.h eµfa?????ta? d?? f????)
#include "platform.h"
#include "uart.h"
#include "queue.h"
#include "timer.h"
#include "gpio.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_usart.h"
#include "i2c.h"
#include "math.h"
#include <stdint.h>
#include "delay.h"

#define BAUD_RATE 9600
#define COUNT_RATE 1000 // 1ms
#define BUFFER_SIZE 10
//Writing is done by sending the slave address in write mode (RW = '0'),
//resulting in slave address 111011X0 pg 29
#define BMP280_ADDR_WRITE   (0x76 << 1) //<<1 gia to RW
//nabling/disabling the measurement and oversampling settings are selected
//through the osrs_p[2:0] bits in control register 0xF4
#define SAMPLING_CONTROL_ADRESS 0xF4
//The IIR filter can be configured using the filter[2:0] bits in control register 0xF5
#define IIR_FILTER_CONTROL_ADRESS 0xF5
//Data readout is done by starting a burst read from 0xF7 to 0xFC.
#define REG_MEASUREMENT_ADRESS 0xF7

#define BMP280_REG_CHIP_ID  0xD0
#define BMP280_REG_RESET    0xE0
#define BMP280_RESET_CMD    0xB6

#define ALERT_PRESSURE_DROP_PA  1000.0   // 10 hPa = 1000 Pa
#define ALERT_DROP_RATE_PA      500.0    // 5 hPa/10s = 500 Pa
#define DEFAULT_P0_PA           101325.0 // 1013.25 hPa se Pa

//oura
Queue rx_queue;

// timer gloabal variables
volatile uint32_t count = 0;
uint32_t led_start_tick = 0;
uint32_t uart_start_tick = 0;
uint32_t ten_sec_tick = 0;

double pressure_10s_ago = 0;

//FSM stages
typedef enum {
    active_mode,
    eco_sleep_mode,
    alert_state
} DeviceState_t;

//table 4-5 sel 12
typedef enum {
    Ultra_low_power,
    Low_power,
    Standard_resolution,
    High_resolution,
    Ultra_high_resolution
} power_state_t;

//table 10 sel 15
typedef enum {
    sleep_mode,
    forced_mode,
    normal_mode
} mode_settings_t;

DeviceState_t currentState = active_mode;
DeviceState_t prev_state = active_mode;

//gloabal flags
volatile bool nucleo_button_pressed = false;
volatile bool outside_button_pressed = false;
volatile bool alert_state_enabled = false;
volatile bool irr_filter_enabled = false;

double pressure_history[10] = {0};
int history_idx = 0;
// FIX: counter ??a ?a ?????µe p?se? ?????e? µet??se?? ?p?????? st?? circular buffer
int history_count = 0;

////////////////////////////////    ISR
//bale xarakthra sthn oura
void my_uart_rx_callback(uint8_t rx_char) {
    if (!queue_is_full(&rx_queue)) queue_enqueue(&rx_queue, rx_char);
}

//timer
void timer_callback_isr(void) {
    count++;
}

//otan paththei kathe koumpi ti flags allazoun
/*
void outside_button_isr(int status) {
    static uint32_t last_outside = 0;
    if((count - last_outside) > 200) {
        outside_button_pressed = true;
        last_outside = count;
        printf("Outside ISR triggered\n"); // DEBUG
    }
}*/

void nucleo_button_isr(int status) {
    static uint32_t last_nucleo = 0;
    if((count - last_nucleo) > 200) {
        nucleo_button_pressed = true;
        last_nucleo = count;
    }
}
/*
void gpio_common_callback(int status) {
    (void)status; // not used
    if (gpio_get(P_SW)) {
        nucleo_button_isr(0);
    }
    if (gpio_get(PB_4)) {
        outside_button_isr(0);
    }
}*/

///////////////////////////////

//////////////////////////// BMP280 communications
double bmp280_compensate_P_double(long signed int adc_P);
double bmp280_compensate_T_double(long signed int adc_T);
double IIR_filter(double data_filtered_old, double data_ADC);
double height_measurement(double *P, double *P0);
void print_sensor_data(double T, double P, double H);
void print_status_report(double P0, double current_P);
void toggle_IIR_filter(void);


#define CAL_REG_AMOUNT 24

//table 17 sel 21
const uint8_t calibration_registers[] = {
    0x88, 0x89, // dig_T1
    0x8A, 0x8B, // dig_T2
    0x8C, 0x8D, // dig_T3
    0x8E, 0x8F, // dig_P1
    0x90, 0x91, // dig_P2
    0x92, 0x93, // dig_P3
    0x94, 0x95, // dig_P4
    0x96, 0x97, // dig_P5
    0x98, 0x99, // dig_P6
    0x9A, 0x9B, // dig_P7
    0x9C, 0x9D, // dig_P8
    0x9E, 0x9F,  // dig_P9
};

//diaxwrismos se signed kai usigned analoga me to table 17
uint16_t dig_T1;
int16_t  dig_T2, dig_T3;
uint16_t dig_P1;
int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;

//selida 28 i2c write protocol
void bmp280_i2c_write(uint8_t reg_address, uint8_t reg_data) {
    uint8_t buf[2] = {reg_address, reg_data};
    i2c_write(BMP280_ADDR_WRITE, buf, 2);
}

//selida 29 i2c read protocol
void bmp280_i2c_read(uint8_t start_reg, uint8_t *buffer, uint8_t len) {
    i2c_write(BMP280_ADDR_WRITE, &start_reg, 1);
    i2c_read(BMP280_ADDR_WRITE, buffer, len);
}


void init_bmp280() {
    bmp280_i2c_write(BMP280_REG_RESET, BMP280_RESET_CMD);
    delay_us(3000); //startup time ~2ms

    uint8_t chip_id = 0;
    bmp280_i2c_read(BMP280_REG_CHIP_ID, &chip_id, 1); // 0xD0
    if(chip_id != 0x58) {
        printf("BMP280 NOT FOUND! ID=0x%02X\r\n", chip_id);
        return; // stamata an den vrethei o sensor
    }
    printf("BMP280 OK\r\n");

    uint8_t dig_T_and_P[CAL_REG_AMOUNT];
    uint16_t dig_T_local[3];
    uint16_t dig_P_local[9];

    uint8_t calib_start_addr = 0x88;
    bmp280_i2c_read(calib_start_addr, dig_T_and_P, CAL_REG_AMOUNT);

    int t_index = 0;
    int p_index = 0;
    for(int i = 0; i < CAL_REG_AMOUNT; i += 2) {
        if(i < 6) {
            dig_T_local[t_index] = (uint16_t)(((uint16_t)dig_T_and_P[i+1] << 8) | (uint16_t)dig_T_and_P[i]);
            t_index++;
        } else {
            dig_P_local[p_index] = (uint16_t)(((uint16_t)dig_T_and_P[i+1] << 8) | (uint16_t)dig_T_and_P[i]);
            p_index++;
        }
    }

    // (signed/unsigned) selida 21
    dig_T1 = dig_T_local[0];
    dig_T2 = (int16_t)dig_T_local[1];
    dig_T3 = (int16_t)dig_T_local[2];

    dig_P1 = dig_P_local[0];
    dig_P2 = (int16_t)dig_P_local[1];
    dig_P3 = (int16_t)dig_P_local[2];
    dig_P4 = (int16_t)dig_P_local[3];
    dig_P5 = (int16_t)dig_P_local[4];
    dig_P6 = (int16_t)dig_P_local[5];
    dig_P7 = (int16_t)dig_P_local[6];
    dig_P8 = (int16_t)dig_P_local[7];
    dig_P9 = (int16_t)dig_P_local[8];
	
		//printf("dig_T1=%u, dig_T2=%d, dig_T3=%d\r\n", dig_T1, dig_T2, dig_T3);
		//printf("dig_P1=%u, dig_P2=%d\r\n", dig_P1, dig_P2);
		//printf("dig_P3=%d, dig_P4=%d, dig_P5=%d\r\n", dig_P3, dig_P4, dig_P5);
		//printf("dig_P6=%d, dig_P7=%d, dig_P8=%d, dig_P9=%d\r\n", dig_P6, dig_P7, dig_P8, dig_P9);
    //enable IIR filter
    toggle_IIR_filter(); 
}

//table 5 sel 13
uint8_t oversamping_setting(power_state_t state) {
    switch (state) {
        case Ultra_low_power:
            return 0x1;
            break;
        case Low_power:
            return 0x2;
            break;
        case Standard_resolution:
            return 0x3;
            break;
        case High_resolution:
            return 0x4;
            break;
        case Ultra_high_resolution:
            return 0x5;
            break;
        default:
            return 0x4;
            break;
    }
}

//table 10 sel 15
uint8_t mode_set(mode_settings_t state) {
    switch (state) {
        case sleep_mode:
            return 0x0;
            break;
        case forced_mode:
            return 0x1;
            break;
        case normal_mode:
            return 0x3;
            break;
        default:
            return 0x3;
            break;
    }
}

uint8_t reg_8bit_fill(power_state_t osrs_t, power_state_t osrs_p, mode_settings_t mode) {
    // osrs_t(7-5) || osrs_p(4-2) || mode (1-0) table 18 sel 24
    return (oversamping_setting(osrs_t) << 5) | (oversamping_setting(osrs_p) << 2) | mode;
}

void bmp280_set_sampling() {
    // oversampling options selida 13
    uint8_t ctrl_meas_addr = SAMPLING_CONTROL_ADRESS;
    uint8_t ctrl_meas_value;

    if(currentState == active_mode) {
        // Temp x8, Press x8, Normal Mode
        ctrl_meas_value = reg_8bit_fill(High_resolution, High_resolution, normal_mode);
    } else {
        // Temp x2, Press x2, Forced Mode (Eco/Sleep)
        ctrl_meas_value = reg_8bit_fill(Low_power, Low_power, forced_mode);
    }
    bmp280_i2c_write(ctrl_meas_addr, ctrl_meas_value);
}


long signed int reg_20bit_fill(uint8_t *raw_data) {
    //table 24-25 selida 26-27 up kai ut [19-12] temp_msb,[11-4] temp_lsb,[3-0] tem_xls to idio kai me press
    return (((uint32_t)raw_data[0] << 12) | ((uint32_t)raw_data[1] << 4) | ((uint32_t)raw_data[2] >> 4));
}

void P_and_T_measurement(uint8_t *raw_data, double *temperature, double *pressure) {
    long signed int adc_P = reg_20bit_fill(raw_data);
    long signed int adc_T = reg_20bit_fill(raw_data + 3);
	
		 printf("adc_P=%ld, adc_T=%ld\r\n", adc_P, adc_T);
	
    double current_temp_compensated = bmp280_compensate_T_double(adc_T);
    double current_press_compensated = bmp280_compensate_P_double(adc_P);

    *temperature = current_temp_compensated;
    //pername apo IIR filtro
    *pressure = IIR_filter(*pressure, current_press_compensated);
}

double IIR_filter(double data_filtered_old, double data_ADC) {//selida 13-14
    if (!irr_filter_enabled) return data_ADC;
    uint8_t reg_value;
    double filter_coefficient;
    uint8_t filter_addr = IIR_FILTER_CONTROL_ADRESS;

    bmp280_i2c_read(filter_addr, &reg_value, 1);

    // apomononoume bits 4-2 (Filter bits) sel 24 table 18
    uint8_t filter_bits = (reg_value & 0x1C) >> 2; //reg_value & 00011100 >>2 --> 00000XXX

    // table 6 sel 14
    switch(filter_bits) {
        case 0x0:  filter_coefficient = 1.0;  break; // Filter OFF
        case 0x1:  filter_coefficient = 2.0;  break;
        case 0x2:  filter_coefficient = 4.0;  break;
        case 0x3:  filter_coefficient = 8.0;  break;
        case 0x4:  filter_coefficient = 16.0; break;
        default: filter_coefficient = 1.0;  break;
    }
    //an einai disabled
    if (filter_coefficient <= 1.0) return data_ADC;
    //an einai enabled isxuei o tupos sel 13
    return (data_filtered_old*(filter_coefficient-1) + data_ADC) / (filter_coefficient);
}

void toggle_IIR_filter(void) {
    uint8_t current_config = 0;
    uint8_t config_addr = IIR_FILTER_CONTROL_ADRESS;

    bmp280_i2c_read(config_addr, &current_config, 1);

    if (irr_filter_enabled) {
        //filter bits = 000 [4-2] 0xF5
        current_config &= ~(0x1C);
        irr_filter_enabled = false;
    } else {
        //current_config = 000 sta [4-2]
        current_config &= ~(0x1C);
        //current_config = 010 sta [4-2]
        current_config |= (0x02 << 2);
        irr_filter_enabled = true;
    }
    //grafoume pisw to filter config
    bmp280_i2c_write(config_addr, current_config);
}

void bmp280_set_standby_time(uint8_t t_sb) {
    uint8_t reg_addr = IIR_FILTER_CONTROL_ADRESS;
    uint8_t reg_value;

    bmp280_i2c_read(reg_addr, &reg_value, 1);

    // mhden ta bits 7,6,5 (t_sb) sel 24 table 18
    reg_value &= ~0xE0;  // 0xE0 = 11100000 binary
    // fortonoume thn timh sto t_sb pou theloume
    reg_value |= (t_sb << 5);

    bmp280_i2c_write(reg_addr, reg_value);
}

double height_measurement(double *P, double *P0) {
    return 44330.0 * (1.0 - pow((*P / *P0), (1.0 / 5.255)));
}
//////////////////////////////////////

int main() {
    uint8_t temp_char;
    char command_buffer[128];
    bool first_loop = true;
    bool measurements_ready = false;
    int idx = 0;
    double pressure_drop_rate = 0;
    double P0 = DEFAULT_P0_PA;
    double P = 0;
    double T = 0;
    double H = 0;
    bool forced_pending = false;
    uint32_t forced_tick = 0;

    queue_init(&rx_queue, 128);

    // arxikopoihsh periferiakwn
    SystemCoreClockUpdate();
    uart_init(BAUD_RATE); // baud rate
    uart_set_rx_callback(my_uart_rx_callback);
    timer_init(COUNT_RATE);// count auksanete kathe COUNT_RATE us
    timer_set_callback(timer_callback_isr);
    i2c_init();
    init_bmp280(); // perilamvanei reset + calibration + IIR filter enable

    //set nucleo led ws ouput
    gpio_set_mode(P_LED_R, Output);
    gpio_set(P_LED_R, 0); // arxikopoihsh se off
    //ekswteriko koumpi
    gpio_set_mode(PB_4, PullDown);
    //gpio_set_trigger(PB_4, Rising);
    //gpio_set_callback(PB_4, gpio_common_callback);
    //ekswteriko led
    gpio_set_mode(PB_10, Output);
    gpio_set(PB_10, 0);
    // nucleo button Init
    gpio_set_mode(P_SW, PullDown); //0 oso den to pataw
    gpio_set_callback(P_SW, nucleo_button_isr);//ti trexw otan patietai
    gpio_set_trigger(P_SW, Rising);// otan pataw koumpi ISR

    // proteraiothtes
    NVIC_SetPriority(TIM2_IRQn, 0);
    NVIC_SetPriority(I2C1_EV_IRQn, 1);
    NVIC_SetPriority(USART2_IRQn, 2);
    NVIC_SetPriority(EXTI4_IRQn, 3);        // ekswteriko koumpi PB4
    NVIC_SetPriority(EXTI15_10_IRQn, 4);   // User Button PC13

    //enables
    uart_enable();
    timer_enable();
    __enable_irq();

    uint8_t raw_data[6];
    uint8_t reg_meas_addr = REG_MEASUREMENT_ADRESS;
    bmp280_set_sampling();
    bmp280_set_standby_time(0x00);

    while(1) {
        if(alert_state_enabled && currentState != alert_state) {
            prev_state = currentState;
            currentState = alert_state;
            alert_state_enabled = false;
        }

        if(nucleo_button_pressed) {
            P0 = P;
            nucleo_button_pressed = false;
            printf("[P0 SET]\r\n");
        }
				
				if (gpio_get(PB_4)) {
						outside_button_pressed = true;
				}

        //oura gia uart epikinwnia
        if (queue_dequeue(&rx_queue, &temp_char)) {
            //emfanish xarakthra sthn othonh
            uart_tx(temp_char);

            if (temp_char == '\r' || temp_char == '\n') {
                command_buffer[idx] = '\0';
                if(idx > 0) {
                    char temp = command_buffer[0];

                    switch (temp)
                    {
                    case 'f':
                        toggle_IIR_filter();
                        break;

                    case 'c':
                        alert_state_enabled = false;
                        gpio_set(PB_10, 0);
                        currentState = prev_state;
                        pressure_drop_rate = 0.0;   // reset rate wste na mhn ksana-pyrodotithei amesos
                        pressure_10s_ago = P;        // reset baseline
                        ten_sec_tick = count;        // reset 10s timer
                        uart_start_tick = count;     // reset uart timer
                        bmp280_set_sampling();       // epanekkinisi sensor
                        printf("\r\n[ALERT CLEARED]\r\n");
                        break;

                    case 's':
                        print_status_report(P0, P);
                        break;

                    default:
                        printf("\r\nUnknown Command! (Use f, c, or s)\r\n");
                        break;
                    }
                }

                idx = 0;
            } else if (idx < 127) {
                command_buffer[idx++] = (char)temp_char;
            }
        }


        switch (currentState) {
            case active_mode:
                //toggle 250ms
                {
                    uint32_t elapsed = (uint32_t)(count - led_start_tick);
                    gpio_set(P_LED_R, (elapsed % 500) < 250);
                }

                if((count - uart_start_tick) >= 1000) {
                    bmp280_i2c_read(reg_meas_addr, raw_data, 6);
                    P_and_T_measurement(raw_data, &T, &P);

                    H = height_measurement(&P, &P0);
                    pressure_history[history_idx] = P;
                    if(history_count < 10) history_count++;

                    if(first_loop) {
                        pressure_10s_ago = P;
                        first_loop = false;
                        measurements_ready = true;
                    }

                    if(count - ten_sec_tick >= 10000) {
                        ten_sec_tick = count;
                        pressure_drop_rate = pressure_10s_ago - P;
                        pressure_10s_ago = P;
                    }
                    history_idx = (history_idx + 1) % 10;

                    print_sensor_data(T, P, H);
                    uart_start_tick = count;
                }

                if(outside_button_pressed) {
                    currentState = eco_sleep_mode;
                    outside_button_pressed = false;
                    led_start_tick = count;
                    uart_start_tick = count;
                    forced_pending = false;
                    bmp280_set_sampling();
                    printf("\r\n[MODE] -> Eco Sleep\r\n");
                }
                break;

            case eco_sleep_mode:
                {
                    uint32_t elapsed = (uint32_t)(count - led_start_tick);
                    gpio_set(P_LED_R, (elapsed % 2000) < 1000);
                }

                if((count - uart_start_tick) >= 5000) {
                    // For a next measurement, forced mode needs to be selected again, sel 15-16
                    if(!forced_pending) {
                        bmp280_set_sampling();
                        forced_tick = count;
                        forced_pending = true;
                    }

                    // 16ms (table 13 sel 18: max measurement time ~9.3ms, µe margin)
                    if(forced_pending && (count - forced_tick) >= 16) {
                        forced_pending = false;

                        bmp280_i2c_read(reg_meas_addr, raw_data, 6);
                        P_and_T_measurement(raw_data, &T, &P);

                        H = height_measurement(&P, &P0);

                        pressure_history[history_idx] = P;
                        if(history_count < 10) history_count++;

                        if(first_loop) {
                            pressure_10s_ago = P;
                            first_loop = false;
                            measurements_ready = true;
                        }

                        if(count - ten_sec_tick >= 10000) {
                            ten_sec_tick = count;
                            pressure_drop_rate = pressure_10s_ago - P;
                            pressure_10s_ago = P;
                        }
                        history_idx = (history_idx + 1) % 10;

                        print_sensor_data(T, P, H);
                        uart_start_tick = count;
                    }
                }

                if(outside_button_pressed) {
                    currentState = active_mode;
                    outside_button_pressed = false;
                    led_start_tick = count;
                    uart_start_tick = count;
                    forced_pending = false;
                    bmp280_set_sampling();
                    bmp280_set_standby_time(0x00); //t_sb 0.5 ms table 11 sel 17
                    printf("\r\n[MODE] -> Active\r\n");
                }
                break;

            case alert_state:
								gpio_set(P_LED_R,0);
                gpio_set(PB_10, 1);
                if((count - uart_start_tick) >= 1000) {
                    printf("[ALERT] Extreme Conditions Detected!\r\n");
                    uart_start_tick = count;
                }
                break;

            default:
                currentState = active_mode;
        }

        if(measurements_ready && currentState != alert_state &&
           (T > 35.0 || (P0 - P) > ALERT_PRESSURE_DROP_PA || pressure_drop_rate > ALERT_DROP_RATE_PA)) {
            alert_state_enabled = true;
        }
    }

    return 0;
}

void print_status_report(double P0, double current_P) {
    printf("\r\n--- STATUS REPORT ---\r\n");

    // Print the current operating mode
    printf("Mode: ");
    if(currentState == active_mode) printf("Active\r\n");
    else if(currentState == eco_sleep_mode) printf("Eco Sleep\r\n");
    else printf("Alert\r\n");
    // Print the status of the IIR filter
    printf("IIR Filter: ");
    // FIX: printf µe format specifier a?t? printf(string) ??a ap?f??? format string vulnerability
    printf("%s", irr_filter_enabled ? "ON\r\n" : "OFF\r\n");

    // Print the reference pressure P0 in hPa
    char buffer[50];
    snprintf(buffer, sizeof(buffer), "P0: %.2f hPa\r\n", P0/100.0);
    printf("%s", buffer);

    // Print the last 10 pressure readings from history (µe t? se??? p?? ??f???a?)
    printf("Last 10 Pressure Readings (hPa):\r\n");
    // FIX: e?t?p?s? µ??? ??????? µet??se?? — ap?f??? e?t?p?s?? 0.0 ??a ?e?? slots
    int valid = history_count < 10 ? history_count : 10;
    // o pa?a??te??? index: a? ? buffer e??a? ?eµ?t??, e??a? t? history_idx (t? ep?µe?? p?? ?a ??afte?)
    // a? de? e??a? ?eµ?t??, ?e????µe ap? 0
    int start = (history_count >= 10) ? history_idx : 0;
    for(int i = 0; i < valid; i++) {
        int idx = (start + i) % 10;
        snprintf(buffer, sizeof(buffer), "%d: %.2f hPa\r\n", i+1, pressure_history[idx]/100.0);
        printf("%s", buffer); // FIX: printf µe format specifier
    }
    printf("---------------------\r\n");
}


void print_sensor_data(double T, double P, double H) {
    char msg[100];
    // Format and print the sensor data: Temperature (C), Pressure (hPa), Height (m)
    // FIX: printf µe format specifier a?t? printf(msg) ??a ap?f??? format string vulnerability
    snprintf(msg, sizeof(msg), "T: %.2f C, P: %.2f hPa, H: %.2f m\r\n", T, P/100.0, H);
    printf("%s", msg);
}

// Returns temperature in DegC, double precision. Output value of "51.23" equals 51.23 DegC.
// t_fine carries fine temperature as global value
long signed int t_fine;
double bmp280_compensate_T_double(long signed int adc_T) {
    double var1, var2, T;

    // Calculate intermediate variables for temperature compensation
    // Magic numbers are based on BMP280 calibration coefficients
    var1 = (((double)adc_T)/16384.0 - ((double)dig_T1)/1024.0) * ((double)dig_T2);
    var2 = ((((double)adc_T)/131072.0 - ((double)dig_T1)/8192.0) *
            (((double)adc_T)/131072.0 - ((double) dig_T1)/8192.0)) * ((double)dig_T3);
    t_fine = (long signed int)(var1 + var2); // Store fine temperature for later use in pressure compensation
    T = (var1 + var2) / 5120.0; // Calculate final temperature in Celsius

    return T;
}

// Returns pressure in Pa as double. Output value of "96386.2" equals 96386.2 Pa = 963.862 hPa
double bmp280_compensate_P_double(long signed int adc_P) {
    double var1, var2, p;

    // Calculate intermediate variables for pressure compensation
    var1 = ((double)t_fine/2.0) - 64000.0; // Use fine temperature from previous calculation
    var2 = var1 * var1 * ((double)dig_P6) / 32768.0;
    var2 = var2 + var1 * ((double)dig_P5) * 2.0;
    var2 = (var2/4.0)+(((double)dig_P4) * 65536.0);
    var1 = (((double)dig_P3) * var1 * var1 / 524288.0 + ((double)dig_P2) * var1) / 524288.0;
    var1 = (1.0 + var1 / 32768.0)*((double)dig_P1);
    if (var1 == 0.0) {
        return 0; // avoid exception caused by division by zero
    }
    p = 1048576.0 - (double)adc_P;
    p = (p - (var2 / 4096.0)) * 6250.0 / var1;
    var1 = ((double)dig_P9) * p * p / 2147483648.0;
    var2 = p * ((double)dig_P8) / 32768.0;
    p = p + (var1 + var2 + ((double)dig_P7)) / 16.0; // Calculate final pressure in Pa

    return p;
}