#define F_CPU 14.7456E6
#include <nlib/adc.h>
#include <util/delay.h>

int main()
{
    const int ledPin = PD7;
    const int outPin1 = PD5;
    const int outPin2 = PD6;

    n_adc_enable(200E3);
    n_adc_set_ref(N_ADC_AVCC);
    DDRD = (1 << ledPin) | (1 << outPin1) | (1 << outPin2);

    bool flip = true;
    while(1)
    {
        if(flip)
        {
            PORTD |= (1 << outPin1);
            PORTD &= ~(1 << outPin2);
            if(n_adc_read(N_ADC0) < 512)
            {
                PORTD |= (1 << ledPin);
            }
            else
            {
                PORTD &= ~(1 << ledPin);
            }
        }
        else
        {
            PORTD |= (1 << outPin2);
            PORTD &= ~(1 << outPin1);
            if(n_adc_read(N_ADC0) > 512)
            {
                PORTD |= (1 << ledPin);
            }
            else
            {
                PORTD &= ~(1 << ledPin);
            }
        }

        flip = !flip;
        _delay_ms(1000);
    }
    return 0;
}
