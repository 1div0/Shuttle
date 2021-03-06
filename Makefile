TARGET = Shuttle
SOURCES = $(TARGET).c mediactrl.c
CFLAGS = -O3 -W -Wall
PKG_CONFIG = pkg-config

CFLAGS += `$(PKG_CONFIG) --cflags liblo`
LIBS := `$(PKG_CONFIG) --libs liblo`

INSTALL_DIR = /usr/local/bin

ifdef DEBUG
CFLAGS += -g
endif # DEBUG

objects := $(patsubst %.c,%.o,$(wildcard $(SOURCES)))

all: $(TARGET)

install: all
	install $(TARGET) $(INSTALL_DIR)

$(TARGET): $(objects)
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

clean:
	$(RM) $(TARGET) $(objects)

$(TARGET).o: $(TARGET).c mediactrl.h
mediactrl.o: mediactrl.c mediactrl.h
