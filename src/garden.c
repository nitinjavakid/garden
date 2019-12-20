#define F_CPU 16E6
#include <avr/power.h>
#include <delay.h>
#include <config.h>
#include <avr/io.h>
#include "config.h"
#include <string.h>
#include <stdlib.h>

typedef char pin_t;
#define DIRECTION_OUT 1
#define DIRECTION_IN  0

pin_t atopin(const char *pin)
{
    if(strcasecmp(pin, "none") == 0)
        return -1;

    return ((((pin[0] - 'A') & 0xf) << 4) | (atoi(pin + 1) & 0xf));
}

void set_direction(pin_t pin, int direction)
{
    int bit;
    if(pin == -1)
    {
        return;
    }

    bit = (pin & 0xf);
    switch(((pin & 0xf0) >> 4) + 'A')
    {
    case 'B':
        DDRB = ((direction & 0x1) << bit) | (DDRB & (~(0x1 << bit)));
        break;
    case 'C':
        DDRC = ((direction & 0x1) << bit) | (DDRC & (~(0x1 << bit)));
        break;
    case 'D':
        DDRD = ((direction & 0x1) << bit) | (DDRD & (~(0x1 << bit)));
        break;
    }
}

void set_pin(pin_t pin, int val)
{
    int bit;
    if(pin == -1)
    {
        return;
    }

    bit = (pin & 0xf);
    switch(((pin & 0xf0) >> 4) + 'A')
    {
    case 'B':
        PORTB = ((val & 0x1) << bit) | (PORTB & (~(0x1 << bit)));
        break;
    case 'C':
        PORTC = ((val & 0x1) << bit) | (PORTC & (~(0x1 << bit)));
        break;
    case 'D':
        PORTD = ((val & 0x1) << bit) | (PORTD & (~(0x1 << bit)));
        break;
    }
}

int get_pin(pin_t pin)
{
    int bit;
    if(pin == -1)
    {
        return 0;
    }

    bit = (pin & 0xf);
    switch(((pin & 0xf0) >> 4) + 'A')
    {
    case 'B':
        return (PINB & (0x1 << bit)) != 0 ? 1 : 0;
        break;
    case 'C':
        return (PINC & (0x1 << bit)) != 0 ? 1 : 0;
        break;
    case 'D':
        return (PIND & (0x1 << bit)) != 0 ? 1 : 0;
        break;
    }

    return 0;
}

int supply_till_water_limit(
    pin_t motor_pin,
    pin_t led_pin,
    pin_t level1_pin,
    pin_t level2_pin)
{
    int level1 = get_pin(level1_pin);
    int level2 = get_pin(level2_pin);
    set_pin(led_pin, 1);
    set_pin(motor_pin, 1);
    while((!level1) && (!level2))
    {
        level1 = get_pin(level1_pin);
        level2 = get_pin(level2_pin);
        n_delay_wait(1, N_DELAY_IDLE);
    }
    set_pin(led_pin, 0);
    set_pin(motor_pin, 0);
    return level2;
}

void fill_all(
    pin_t motor_pin,
    pin_t led_pin,
    pin_t level1_pin,
    pin_t level2_pin)
{
    while(!supply_till_water_limit(motor_pin, led_pin, level1_pin, level2_pin))
    {
        int count = 30;
        while(count)
        {
            n_delay_wait(1, N_DELAY_POWER_DOWN);
            --count;
            if(get_pin(level2_pin))
            {
                return;
            }
        }
    }
}

void blink(pin_t pin, int count, int sleep)
{
    while(count)
    {
        --count;
        set_pin(pin, 1);
        n_delay_loop(sleep * 1000 / 2);
        set_pin(pin, 0);
        n_delay_loop(sleep * 1000 / 2);
    }
}

int main()
{
    pin_t motor_pin;
    pin_t led_pin;
    pin_t level1_pin;
    pin_t level2_pin;
    uint32_t count = 0;

    n_delay_init(F_CPU);
    motor_pin = atopin(MOTOR_PIN);
    led_pin = atopin(LED_PIN);
    level1_pin = atopin(LEVEL1_PIN);
    level2_pin = atopin(LEVEL2_PIN);

    set_direction(led_pin, DIRECTION_OUT);
    set_direction(motor_pin, DIRECTION_OUT);
    set_direction(level1_pin, DIRECTION_IN);
    set_direction(level2_pin, DIRECTION_IN);

    while(1)
    {
        fill_all(motor_pin, led_pin, level1_pin, level2_pin);
        blink(led_pin, 5, 1);
        for(count = 0; count < INTERVAL_HOURS * 60; ++count)
        {
            n_delay_wait(58, N_DELAY_POWER_DOWN);
            blink(led_pin, 2, 1);
        }
    }

    return 0;
}
