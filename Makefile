# Compilar con la imagen ps2dev (ver README) o con PS2SDK + gsKit instalados
EE_BIN  = fps.elf
EE_OBJS = main.o assets.o

GSKIT ?= $(PS2DEV)/gsKit

EE_INCS   += -I$(GSKIT)/include
EE_LDFLAGS += -L$(GSKIT)/lib
EE_LIBS   = -lgskit -ldmakit -lpad -lm -lc -lkernel

all: $(EE_BIN)

clean:
	rm -f $(EE_BIN) $(EE_OBJS)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
