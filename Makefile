TARGET = Shuttle
CFILES = $(TARGET).c htmsocket.c mediactrl.c
LIBS = -lOSC

include $(PEAK)/include/make/definitions
include $(PROGRAMRULES)
