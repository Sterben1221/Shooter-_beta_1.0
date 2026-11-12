# Compilar con la imagen ps2dev (ver README) o con PS2SDK + gsKit instalados
# make              -> con sonido de pasos (audsrv)
# make USE_SOUND=0  -> sin sonido
EE_BIN  = fps.elf
EE_OBJS = main.o assets.o sounds.o

GSKIT ?= $(PS2DEV)/gsKit

EE_INCS    += -I$(GSKIT)/include
EE_LDFLAGS += -L$(GSKIT)/lib
EE_LIBS     = -lgskit -ldmakit -lpad -lm -lc -lkernel

USE_SOUND ?= 1
ifeq ($(USE_SOUND),1)
EE_CFLAGS += -DUSE_SOUND
EE_OBJS   += audsrv_irx.o
EE_LIBS   := -laudsrv -lpatches $(EE_LIBS)
endif

all: $(EE_BIN)

audsrv_irx.c: $(PS2SDK)/iop/irx/audsrv.irx
	$(PS2SDK)/bin/bin2c $< $@ audsrv_irx

clean:
	rm -f $(EE_BIN) $(EE_OBJS) audsrv_irx.c

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
