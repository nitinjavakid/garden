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
#include <wifi.h>
#include <esp8266.h>
#include <twi.h>
#include <debug.h>
#include <http.h>
#include <stdio.h>
#include "config.h"

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

int               flip = 0;
pin_t             ledPin;
uint32_t          timestamp = 0;
unsigned int      delay = 8;
int               noOfPlants = 0;
plant_t           *plants = NULL;
volatile uint32_t counter = 0;
n_io_handle_t     twi_handle = NULL;
n_wifi_t          wifi_handle = NULL;

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
    N_DEBUG("Initializing with %s\r\n", string);
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

    N_DEBUG("DONE");

    free((char *)string);
}

void waterPlant(int idx)
{
    N_DEBUG("Trying to connect");
    n_io_handle_t tcp = NULL;
    n_http_request_t request = NULL;
    n_http_response_t response = NULL;
    char data[20];
    char len[4];

    tcp = n_wifi_open_io(wifi_handle, N_WIFI_IO_TYPE_TCP, "an.andnit.in", 80, 100);
    if(tcp == NULL)
    {
        N_DEBUG("Unable to open connection");
        return;
    }

    N_DEBUG("ESP8266 connecting");

    request = n_http_new_request();
    if(request == NULL)
    {
        N_DEBUG("Unable to allocate request");
        n_io_close(tcp);
        return;
    }

    n_http_request_set_uri(request, "/record.php");
    n_http_request_set_method(request, "POST");
    n_http_set_header(request, "Host", "an.andnit.in");
    n_http_set_header(request, "Connection", "close");
    n_http_set_header(request, "Content-Type", "application/x-www-form-urlencoded");
    snprintf(data, sizeof(data) - 1, "i=%d", idx);
    snprintf(len, sizeof(len), "%d", strlen(data));
    n_http_set_header(request, "Content-Length", len);

    n_http_request_write_to_stream(request, tcp);
    n_http_free_object(request);

    n_io_printf(tcp, "%s", data);

    N_DEBUG("ESP8266 sent");

    response = n_http_new_response();
    if(response == NULL)
    {
        n_io_close(tcp);
        return;
    }
    n_http_set_header(response, "Content-Length", NULL);
    N_DEBUG("Reading response");
    n_http_response_read_from_stream(response, tcp);
    N_DEBUG("Content size: %d", atoi(n_http_get_header(response, "Content-Length")));
    n_http_free_object(response);
    n_io_close(tcp);
    N_DEBUG("ESP8266 closed");

}

void recordPlant(int idx, int val, int flip)
{
    N_DEBUG("Trying to connect");
    char data[20];
    char len[4];
    n_io_handle_t tcp = NULL;
    n_http_request_t request = NULL;
    n_http_response_t response = NULL;

    tcp = n_wifi_open_io(wifi_handle, N_WIFI_IO_TYPE_TCP, "an.andnit.in", 80, 100);
    if(tcp == NULL)
    {
        N_DEBUG("Unable to open connection");
        return;
    }

    N_DEBUG("ESP8266 connecting");

    request = n_http_new_request();
    if(request == NULL)
    {
        N_DEBUG("Unable to allocate request");
        n_io_close(tcp);
        return;
    }

    snprintf(data, sizeof(data) - 1, "i=%d&v=%d&f=%d", idx, val, flip);

    n_http_request_set_uri(request, "/record.php");
    n_http_request_set_method(request, "POST");
    n_http_set_header(request, "Host", "an.andnit.in");
    n_http_set_header(request, "Connection", "close");
    n_http_set_header(request, "Content-Type", "application/x-www-form-urlencoded");
    snprintf(len, sizeof(len), "%d", strlen(data));
    n_http_set_header(request, "Content-Length", len);

    n_http_request_write_to_stream(request, tcp);
    n_http_free_object(request);

    n_io_printf(tcp, "%s", data);
    N_DEBUG("ESP8266 sent");

    response = n_http_new_response();
    if(response == NULL)
    {
        n_io_close(tcp);
        return;
    }
    n_http_set_header(response, "Content-Length", NULL);
    n_http_response_read_from_stream(response, tcp);
    N_DEBUG("Content size: %d", atoi(n_http_get_header(response, "Content-Length")));
    n_http_free_object(response);
    n_io_close(tcp);
    N_DEBUG("ESP8266 closed");
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
        if(val > 512)
        {
            waterPlant(idx);
        }
    }

    recordPlant(idx, val, flip);

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

    N_DEBUG("Plants %lu %d\r\n", timestamp, noOfPlants);
    for(idx = 0; idx < noOfPlants; ++idx)
    {
        N_DEBUG("Plant %d %d\r\n", idx, plants[idx].noOfRecords);
        for(ridx = 0; ridx < plants[idx].noOfRecords; ++ridx)
        {
            N_DEBUG("%lu", ((uint32_t) plants[idx].records[ridx].time * delay) + timestamp);
        }
    }

    N_DEBUG("DONE");
    sei();
}

int main()
{
    char *line = NULL;
    int i;
    n_wifi_ap_node_t *nodes = NULL, *iter = NULL;
    volatile n_io_handle_t tcp = NULL, usart_handle = NULL;

    twi_handle = n_twi_new_master_io(0x04, F_CPU, 100000);
    n_debug_init(twi_handle);

    N_DEBUG("System booted");

    n_delay_init(F_CPU);

    n_delay_loop(10000);

    n_usart_enable(N_USART_MODE_ASYNC, N_USART_8BIT, N_USART_PARITY_NONE, N_USART_STOPBIT1, 9600);

    usart_handle = n_usart_new_io(100);

    N_DEBUG("UART enabled");

    wifi_handle = n_esp8266_open_wifi(usart_handle);

    N_DEBUG("ESP8266 enabled");

    n_wifi_restart(wifi_handle);

    N_DEBUG("ESP8266 restarted");

    n_wifi_status(wifi_handle);

    N_DEBUG("ESP8266 status ok");

    n_wifi_set_mode(wifi_handle, N_WIFI_MODE_STA);

    N_DEBUG("ESP8266 station mode set");

    n_wifi_connect(wifi_handle, WIFI_SSID, WIFI_PASSWORD);

    N_DEBUG("ESP8266 connected");

    n_wifi_set_network(wifi_handle, WIFI_IP, WIFI_GATEWAY, WIFI_NETMASK);

    N_DEBUG("ESP8266 network setup done");

    reconfigure("1,60,D7,1,D5,D6,C0");

    while(1)
    {
        process();
        power_all_disable();
        n_delay_wait(delay, N_DELAY_POWER_DOWN);
        power_all_enable();
    }

    return 0;
}
