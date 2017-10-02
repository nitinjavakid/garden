#define F_CPU 16E6
#include <adc.h>
#include <usart.h>
#include <string.h>
#include <stdarg.h>
#include <delay.h>
#include <stdlib.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <util/delay.h>

typedef char pin_t;

typedef struct _record {
    int time;
} record_t;

typedef struct _config {
    pin_t forwardPin;
    pin_t reversePin;
    pin_t adcPin;
} config_t;

typedef struct _plant {
    config_t  config;
    int       noOfRecords;
    record_t *records;
} plant_t;

int          flip = 0;
pin_t        ledPin;
uint32_t     timestamp = 0;
unsigned int delay = 8;
int          noOfPlants = 0;
plant_t      *plants = NULL;
volatile uint32_t     counter = 0;

#ifdef DEBUG
#define DBGPRINT(fmt, ...) uprintf("DEBUG: " fmt, ##__VA_ARGS__)
#else
#define DBGPRINT(fmt, ...) do { } while(0)
#endif

void uprintf(const char *fmt, ...)
{
    char *str = NULL;
    int len = 0;
    int idx = 0;
    va_list ap;
    va_start(ap, fmt);
    len = vsnprintf(NULL, 0, fmt, ap);
    str = calloc(len + 1, 1);
    len = vsnprintf(str, len + 1, fmt, ap);
    va_end(ap);
    for(idx = 0; idx < len + 1; ++idx)
    {
        n_usart_write(str[idx]);
    }
    free(str);
}

pin_t atopin(const char *pin)
{
    if(strcasecmp(pin, "none") == 0)
        return -1;

    return ((((pin[0] - 'A') & 0xf) << 4) | (atoi(pin + 1) & 0xf));
}

void setDirection(pin_t pin, int direction)
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

void setPin(pin_t pin, int val)
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

int getPin(pin_t pin)
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
        return (PORTB & (0x1 << bit)) != 0 ? 1 : 0;
        break;
    case 'C':
        return (PORTC & (0x1 << bit)) != 0 ? 1 : 0;
        break;
    case 'D':
        return (PORTD & (0x1 << bit)) != 0 ? 1 : 0;
        break;
    }

    return 0;
}

void freePlants()
{
    int idx = 0;
    for(idx = 0; idx < noOfPlants; ++idx)
    {
        free(plants[idx].records);
    }

    free(plants);
}

/*
 * Configure from string, params are comma seperated
 * timestamp
 * delay
 * ledPin
 * noOfPlants
 * for each plants
 *     forwardPin
 *     reversePin
 *     adcPin
 */

void reconfigure(const char *string)
{
    string = strdup(string);
    char *curptr = (char *) string;
    char *nextptr = NULL;
    int idx = 0;

    freePlants();
    DBGPRINT("Initializing with %s\r\n", string);
    if((nextptr = strchr(curptr, ',')) != NULL)
    {
        *nextptr = '\0';
        timestamp = strtoul(curptr, NULL, 0);
        curptr = nextptr + 1;
    }

    if((nextptr = strchr(curptr, ',')) != NULL)
    {
        *nextptr = '\0';
        delay = atoi(curptr);
        curptr = nextptr + 1;
    }

    if((nextptr = strchr(curptr, ',')) != NULL)
    {
        *nextptr = '\0';
        ledPin = atopin(curptr);
        curptr = nextptr + 1;
    }

    if((nextptr = strchr(curptr, ',')) != NULL)
    {
        *nextptr = '\0';
        noOfPlants = atoi(curptr);
        curptr = nextptr + 1;
    }

    if(noOfPlants > 0)
    {
        plants = calloc(noOfPlants, sizeof(plant_t));
    }

    for(idx = 0; idx < noOfPlants; ++idx)
    {
        if((nextptr = strchr(curptr, ',')) != NULL)
        {
            *nextptr = '\0';
            plants[idx].config.forwardPin = atopin(curptr);
            curptr = nextptr + 1;
        }

        if((nextptr = strchr(curptr, ',')) != NULL)
        {
            *nextptr = '\0';
            plants[idx].config.reversePin = atopin(curptr);
            curptr = nextptr + 1;
        }

        nextptr = strchr(curptr, ',');
        if(nextptr != NULL)
        {
            *nextptr = '\0';
        }

        plants[idx].config.adcPin = atopin(curptr);

        if(nextptr != NULL)
        {
            curptr = nextptr + 1;
        }
    }

    uprintf("DONE\r\n");

    free((char *)string);
}

void waterPlant(int idx)
{
    cli();
    DBGPRINT("Watering %d\r\n", idx);
    if(idx >= noOfPlants)
    {
        sei();
        return;
    }

    volatile plant_t *plant = &plants[idx];
    plant->records = realloc(plant->records,
                             (plant->noOfRecords + 1) * sizeof(record_t));

    plant->records[plant->noOfRecords++].time = counter;

    sei();
}

void processPlant(int idx)
{
    int val = 0;
    setDirection(plants[idx].config.forwardPin, 1);
    setDirection(plants[idx].config.reversePin, 1);
    if(flip)
    {
        setPin(plants[idx].config.forwardPin, 1);
        setPin(plants[idx].config.reversePin, 0);
        val = n_adc_read(plants[idx].config.adcPin);
        DBGPRINT("FLIP %d\r\n", val);
        if(val < 512)
        {
            waterPlant(idx);
        }
    }
    else
    {
        setPin(plants[idx].config.reversePin, 1);
        setPin(plants[idx].config.forwardPin, 0);
        val = n_adc_read(plants[idx].config.adcPin);
        DBGPRINT("!FLIP %d\r\n", val);
        if(val > 512)
        {
            waterPlant(idx);
        }
    }

    DBGPRINT("DDRD = %d\r\n", DDRD);
    DBGPRINT("PORTD = %d\r\n", PORTD);
    DBGPRINT("ADMUX = %d\r\n", ADMUX);
    DBGPRINT("ADCSRA = %d\r\n", ADCSRA);
    DBGPRINT("ADCSRB = %d\r\n", ADCSRB);
    DBGPRINT("PRR = %d\r\n\r\n", PRR);

    setPin(plants[idx].config.forwardPin, 0);
    setPin(plants[idx].config.reversePin, 0);
}

void process()
{
    int idx = 0;
    setDirection(ledPin, 1);
    setPin(ledPin, 1);

    counter++;
    n_adc_set_ref(N_ADC_AVCC);
    n_adc_enable(200E3);

    for(idx = 0; idx < noOfPlants; ++idx)
    {
        processPlant(idx);
    }

    flip = !flip;
    setPin(ledPin, 0);
}

void dumpInfo()
{
    int idx = 0;
    int ridx = 0;
    cli();

    uprintf("Plants %lu %d\r\n", timestamp, noOfPlants);
    for(idx = 0; idx < noOfPlants; ++idx)
    {
        uprintf("Plant %d %d\r\n", idx, plants[idx].noOfRecords);
        for(ridx = 0; ridx < plants[idx].noOfRecords; ++ridx)
        {
            uprintf("%lu\r\n", ((uint32_t) plants[idx].records[ridx].time * delay) + timestamp);
        }
    }

    uprintf("DONE\r\n");
    sei();
}

char recvBuffer[50];
char recvBufferIdx = 0;
volatile char commandSet = 0;

ISR(USART_RX_vect)
{
    int byte = n_usart_read();
    n_usart_write(byte);

    if(recvBufferIdx >= sizeof(recvBuffer))
    {
        recvBufferIdx = 0;
    }

    if(byte == ';')
    {
        uprintf("\r\n");
        recvBuffer[recvBufferIdx] = '\0';
        if(strncasecmp(recvBuffer, "init", 4) == 0)
        {
            reconfigure(recvBuffer + 5);
        }
        else if(strncasecmp(recvBuffer, "info", 4) == 0)
        {
            dumpInfo();
        }

        recvBufferIdx = 0;
    }
    else if(byte == '!')
    {
        uprintf("\r\n");
        recvBufferIdx = 0;
    }
    else
    {
        recvBuffer[recvBufferIdx++] = byte;
    }

    recvBuffer[recvBufferIdx] = '\0';
}

void waitForCommand(int maxDelay)
{
    uprintf("\r\n> ");
    commandSet = 0;
    while((maxDelay > 0) && !commandSet)
    {
        maxDelay -= 8;
        power_all_disable();
        power_usart0_enable();
        n_delay_wait(8, N_DELAY_IDLE);
        power_all_enable();
    }
}

ISR(INT0_vect, ISR_NOBLOCK)
{
    power_usart0_enable();
    uprintf("\r\n> ");
    _delay_ms(60000);
    power_usart0_disable();
}

int main()
{
    n_delay_init();
    n_usart_enable(N_USART_8BIT, N_USART_PARITY_NONE, N_USART_STOPBIT1, 9600);
    n_usart_set_interrupt_flag(1, 0);

    uprintf("Booting up...\r\n");

    sei();

    while(noOfPlants == 0)
    {
        waitForCommand(30);
    }

    EICRA = (0 << ISC01) | (1 << ISC00);
    EIMSK = (1 << INT0);

    while(1)
    {
        process();
        power_all_disable();
        n_delay_wait(delay, N_DELAY_POWER_DOWN);
        power_all_enable();
    }

    return 0;
}
