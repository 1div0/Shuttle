TARGET = Shuttle
CFILES = $(TARGET).c htmsocket.c mediactrl.c
CFLAGS=-O3 -W -Wall
LIBS = -lOSC

INSTALL_DIR=/usr/local/bin

OBJ=$(TARGET).o htmsocket.o mediactrl.o

all: $(TARGET)

install: all
	install $(TARGET) ${INSTALL_DIR}

$(TARGET): ${OBJ}
	$(CC) ${CFLAGS} ${OBJ} -o $@

clean:
	$(RM) $(TARGET) $(OBJ)

$(TARGET).o: $(TARGET).c
htmsocket.o: htmsocket.c htmsocket.h
mediactrl.o: mediactrl.c mediactrl.h
