all: clean garden.hex

INCLUDES = -I..
MMCU = atmega328p

SRCS = garden.c
OBJS = ${SRCS:.c=.o}

LIBS = ../nlib/adc.${MMCU}.o

clean:
	rm -f ${OBJS} garden.hex

%.o: %.c
	avr-g++ -Os -c ${INCLUDES} -mmcu=${MMCU} -o $@ $<

garden.hex: ${OBJS}
	avr-g++ -Os ${INCLUDES} -mmcu=${MMCU} -o garden.bin ${OBJS} ${LIBS}
	avr-objcopy -j .text -j .data -O ihex garden.bin $@

