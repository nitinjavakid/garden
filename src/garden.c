#define F_CPU 16E6
#include <adc.h>
#include <usart.h>
#include <string.h>
#include <stdarg.h>
#include <delay.h>
#include <avr/power.h>
#include <util/delay.h>

volatile int counter = 0;
const int ledPin = PD7;
const int outPin1 = PD5;
const int outPin2 = PD6;
int flip = 0;

void uprintf(const char *fmt, ...)
{
    char str[50];
    int len = 0;
    int idx=0;
    va_list ap;
    va_start(ap, fmt);
    len = vsnprintf(str, sizeof(str), fmt, ap);
    va_end(ap);
    for(idx = 0; idx < len + 1; ++idx)
    {
        n_usart_write(str[idx]);
    }
}

void waterPlant(int idx, int val)
{
    uprintf("Watered %d %d %d\r\n", idx, val, counter);
}

void process()
{
    int val = 0;
    DDRD = (1 << ledPin) | (1 << outPin1) | (1 << outPin2);
    PORTD |= (1 << ledPin);

    counter++;
    n_adc_set_ref(N_ADC_AVCC);
    n_adc_enable(200E3);


    if(flip)
    {
        PORTD |= (1 << outPin1);
        PORTD &= ~(1 << outPin2);
        val = n_adc_read(N_ADC0);
        uprintf("Debug: FLIP %d\r\n", val);
        if(val < 512)
        {
            waterPlant(flip, val);
        }
    }
    else
    {
        PORTD |= (1 << outPin2);
        PORTD &= ~(1 << outPin1);
        val = n_adc_read(N_ADC0);
        uprintf("Debug: !FLIP %d\r\n", val);
        if(val > 512)
        {
            waterPlant(flip, val);
        }
    }

    uprintf("ADMUX = %d\r\n", ADMUX);
    uprintf("ADCSRA = %d\r\n", ADCSRA);
    uprintf("ADCSRB = %d\r\n", ADCSRB);
    uprintf("PRR = %d\r\n", PRR);

    flip = !flip;
    PORTD &= ~(1 << ledPin);
}

int main()
{
    n_delay_init();
    n_usart_enable(N_USART_8BIT, N_USART_PARITY_NONE, N_USART_STOPBIT1, 9600);

    uprintf("RESET\r\n");

    DDRD = (1 << ledPin) | (1 << outPin1) | (1 << outPin2);

    while(1)
    {
        process();
        power_all_disable();
        n_delay_wait(8, N_DELAY_POWER_DOWN);
        power_all_enable();
    }

    return 0;
}
