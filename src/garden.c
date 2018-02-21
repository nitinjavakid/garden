#define F_CPU 16E6
#include <adc.h>
#include <usart.h>
#include <string.h>
#include <stdarg.h>
#include <delay.h>
#include <stdlib.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/delay.h>
#include <wifi.h>
#include <esp8266.h>
#include <twi.h>
#include <debug.h>
#include <http.h>
#include <stdio.h>
#include "config.h"

typedef char pin_t;

typedef struct _plant {
    uint32_t index;
    pin_t forwardPin;
    pin_t reversePin;
    pin_t adcPin;
} plant_t;

int               flip = 0;
pin_t             esp8266pin;
int               noOfPlants = 0;
plant_t           *plants = NULL;
volatile unsigned int      delay = 8;
n_io_handle_t     twi_handle = NULL;
n_wifi_t          wifi_handle = NULL;

char *doHTTP(const char *uri, const char *data, size_t size, size_t *retsize);

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
    free(plants);
    plants = NULL;
}

/*
 * Configure from string, params are comma seperated
 * delay
 * noOfPlants
 * for each plants
 *     forwardPin
 *     reversePin
 *     adcPin
 */

void configure()
{
    char *curptr = NULL;
    char *nextptr = NULL;
    int idx = 0;
    char uri[30];
    char *response = NULL;
    size_t retsize = 0;
    snprintf(uri, sizeof(uri), "/api/devices/%" PRIu32 "/config", (uint32_t) DEVICEID);

    response = doHTTP(uri, NULL, 0, &retsize);
    if((response == NULL) || (strcmp(response, "") == 0))
    {
        return;
    }

    N_DEBUG("Initializing with %s\r\n", response);
    curptr = response;
    freePlants();

    if((nextptr = strchr(curptr, ',')) != NULL)
    {
        *nextptr = '\0';
        delay = atoi(curptr);
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
            plants[idx].index = atol(curptr);
            curptr = nextptr + 1;
        }

        if((nextptr = strchr(curptr, ',')) != NULL)
        {
            *nextptr = '\0';
            plants[idx].forwardPin = atopin(curptr);
            curptr = nextptr + 1;
        }

        if((nextptr = strchr(curptr, ',')) != NULL)
        {
            *nextptr = '\0';
            plants[idx].reversePin = atopin(curptr);
            curptr = nextptr + 1;
        }

        nextptr = strchr(curptr, ',');
        if(nextptr != NULL)
        {
            *nextptr = '\0';
        }

        plants[idx].adcPin = atopin(curptr);

        if(nextptr != NULL)
        {
            curptr = nextptr + 1;
        }
    }

    N_DEBUG("DONE");

    free(response);
}

char *doHTTP(const char *uri, const char *data, size_t size, size_t *retsize)
{
    N_DEBUG("Trying to connect");
    char len[10];
    char hostname[30];
    enable_esp();
    n_io_handle_t tcp = NULL;
    n_http_request_t request = NULL;
    n_http_response_t response = NULL;
    char *retval = NULL;
    size_t retlen = 0, alloc_size = 30;
    char ch = 0;
    char chunked = 0;

    tcp = n_wifi_open_io(wifi_handle, N_WIFI_IO_TYPE_TCP, HOSTNAME, PORT, 50);
    if(tcp == NULL)
    {
        N_DEBUG("Unable to open connection");
        disable_esp();
        return NULL;
    }

    N_DEBUG("ESP8266 connecting");

    request = n_http_new_request();
    if(request == NULL)
    {
        N_DEBUG("Unable to allocate request");
        n_io_close(tcp);
        disable_esp();
        return NULL;
    }

    n_http_request_set_uri(request, uri);
    n_http_request_set_method(request, size ? "POST" : "GET");
    snprintf(hostname, sizeof(hostname), "%s", HOSTNAME);
    n_http_set_header(request, "Host", hostname);
    if(size)
    {
        snprintf(len, sizeof(len), "%d", (int) size);
        n_http_set_header(request, "Content-Length", len);
        n_http_set_header(request, "Content-Type", "application/x-www-form-urlencoded");
    }

    n_http_set_header(request, "Connection", "Close");
    n_http_set_header(request, "X-ApiKey", APIKEY);

    n_http_request_write_to_stream(request, tcp);

    n_http_free_object(request);

    if(size)
    {
        n_io_write(tcp, data, size);
    }

    n_io_flush(tcp);
    N_DEBUG("ESP8266 sent");

    response = n_http_new_response();
    if(response == NULL)
    {
        n_io_close(tcp);
        return NULL;
    }

    N_DEBUG("ESP8266 reading response");

    n_http_set_header(response, "Transfer-Encoding", NULL);
    n_http_response_read_from_stream(response, tcp);
    N_DEBUG("Response value: %d", n_http_response_get_status(response));

    if(strstr(n_http_get_header(response, "Transfer-Encoding"), "chunked") != NULL)
    {
        N_DEBUG("Chunked data");
        chunked = 1;
    }

    if(n_http_response_get_status(response) == 200)
    {
        retval = (char *) malloc(alloc_size);
        if(retval)
        {
            unsigned long toread = 0;
            while(1)
            {
                if(chunked && (toread == 0))
                {
                    char line[10];
                    do
                    {
                        n_io_readline(tcp, line, sizeof(line));
                    } while(line[0] == '\r');

                    toread = strtoul(line, NULL, 16);
                    if(toread == 0)
                    {
                        n_io_readline(tcp, line, sizeof(line));
                        break;
                    }
                }

                if((ch = n_io_getch(tcp)) == -1) break;

                if(retlen + 1 == alloc_size)
                {
                    char *newblock = (char *)realloc(retval, alloc_size + 10);
                    if(newblock == NULL)
                    {
                        break;
                    }

                    retval = newblock;
                    alloc_size += 10;
                }

                --toread;
                retval[retlen++] = ch;
            }

            retval[retlen] = '\0';
        }
    }

    n_http_free_object(response);
    n_io_close(tcp);
    N_DEBUG("ESP8266 closed");
    if(retsize)
    {
        *retsize = retlen;
    }

    if(retval)
    {
        N_DEBUG("Returning %d", (int)retlen);
    }
    disable_esp();
    return retval;
}

char *recordPlant(uint32_t idx, int val, int flip)
{
    char data[30];
    char uri[40];
    snprintf(uri, sizeof(uri), "/api/devices/%" PRIu32 "/execute", (uint32_t)DEVICEID);
    snprintf(data, sizeof(data), "i=%" PRIu32 "&v=%d&f=%d", idx, val, flip);
    return doHTTP(uri, data, strlen(data), NULL);
}

void water(int seconds, pin_t forward, pin_t reverse)
{
    if(seconds == 0)
        return;

    setDirection(forward, 1);
    setDirection(reverse, 1);
    setPin(forward, 1);
    setPin(reverse, 0);

    n_delay_wait(seconds, N_DELAY_IDLE);

    setPin(forward, 0);
    setPin(reverse, 0);
}

void processPlant(int idx)
{
    int val = 0;
    char *response = NULL;
    setDirection(plants[idx].forwardPin, 1);
    setDirection(plants[idx].reversePin, 1);
    if(flip)
    {
        setPin(plants[idx].forwardPin, 1);
        setPin(plants[idx].reversePin, 0);
        val = n_adc_read(plants[idx].adcPin);
    }
    else
    {
        setPin(plants[idx].reversePin, 1);
        setPin(plants[idx].forwardPin, 0);
        val = n_adc_read(plants[idx].adcPin);
    }

    response = recordPlant(plants[idx].index, val, flip);
    if((response != NULL) && (strcmp(response, "") != 0))
    {
        char *curptr = response;
        char *nextptr = NULL;
        pin_t forward_pin;
        pin_t reverse_pin;
        int wateringSeconds = 0;
        if((nextptr = strchr(curptr, ',')) != NULL)
        {
            *nextptr = '\0';
            forward_pin = atopin(curptr);
            curptr = nextptr + 1;
        }

        if((nextptr = strchr(curptr, ',')) != NULL)
        {
            *nextptr = '\0';
            reverse_pin = atopin(curptr);
            curptr = nextptr + 1;
        }

        wateringSeconds = atoi(curptr);
        water(wateringSeconds, forward_pin, reverse_pin);
    }

    if(response != NULL)
    {
        free(response);
    }

    setPin(plants[idx].forwardPin, 0);
    setPin(plants[idx].reversePin, 0);
}

void process()
{
    int idx = 0;

    n_adc_set_ref(N_ADC_AVCC);
    n_adc_enable(200E3);

    for(idx = 0; idx < noOfPlants; ++idx)
    {
        processPlant(idx);
    }

    flip = !flip;
}

volatile n_io_handle_t tcp = NULL, usart_handle = NULL;

void enable_esp()
{
    setDirection(esp8266pin, 1);
    setPin(esp8266pin, 1);

    N_DEBUG("ESP Enabled");
    n_delay_loop(5000);

    n_usart_enable(N_USART_MODE_ASYNC, N_USART_8BIT, N_USART_PARITY_NONE, N_USART_STOPBIT1, 9600);

    N_DEBUG("Starting usart");
    usart_handle = n_usart_new_io(100);

    N_DEBUG("UART enabled");

    wifi_handle = n_esp8266_open_wifi(usart_handle);

    n_wifi_restart(wifi_handle);

    n_wifi_status(wifi_handle);

    n_wifi_set_mode(wifi_handle, N_WIFI_MODE_STA);

    n_wifi_connect(wifi_handle, WIFI_SSID, WIFI_PASSWORD);

#if !WIFI_DHCP
    n_wifi_set_network(wifi_handle, WIFI_IP, WIFI_GATEWAY, WIFI_NETMASK);
    N_DEBUG("ESP8266 network setup done");
#endif
}

void disable_esp()
{
    n_wifi_close(wifi_handle);
    n_io_close(usart_handle);
    setDirection(esp8266pin, 1);
    setPin(esp8266pin, 0);
    N_DEBUG("ESP Disabled");
}

uint8_t mcusr_mirror __attribute__ ((section (".noinit")));
void get_mcusr(void) __attribute__((naked)) __attribute__((section(".init3")));
void get_mcusr(void)
{
    mcusr_mirror = MCUSR;
    MCUSR = 0;
    wdt_disable();
}

int main()
{
    char pinstr[3];
    int i;
    pinstr[0] = 'D';
    pinstr[2] = '\0';
    for(i = 2; i <= 7; i++)
    {
        pin_t pin;
        pinstr[1] = '0' + i;
        pin = atopin(pinstr);
        setDirection(pin, 1);
        setPin(pin, 0);
    }

    twi_handle = n_twi_new_master_io(0x04, F_CPU, 100000);
    if(twi_handle != NULL)
    {
        n_debug_init(twi_handle);
    }

    N_DEBUG("System booted");

    n_delay_init(F_CPU);

    esp8266pin = atopin(ESP8266_PIN);

    while(1)
    {
        configure();
        process();
        power_all_disable();
        n_delay_wait(delay, N_DELAY_POWER_DOWN);
        power_all_enable();
    }

    return 0;
}
