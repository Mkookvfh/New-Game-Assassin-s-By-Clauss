# THE ASHEN KNIGHT — PlayStation 2 homebrew (PS2SDK + gsKit)
# Requires PS2DEV / PS2SDK / GSKIT environment variables (ps2dev v2.0.0).

EE_BIN  = ashen-knight.elf
EE_OBJS = build/ps2_main.o build/game.o build/world.o build/actors.o \
          build/mesh.o build/font.o build/materials_data.o \
          build/irx_sio2man.o build/irx_padman.o

EE_INCS    = -I$(PS2DEV)/gsKit/include -Isrc
EE_LDFLAGS = -L$(PS2DEV)/gsKit/lib
EE_LIBS    = -lgskit_toolkit -lgskit -ldmakit -lpad -lm

EE_CFLAGS = -O2 -G0 -fno-common -Wall -Wno-unused-parameter

BIN2C = $(PS2SDK)/bin/bin2c

all: $(EE_BIN)

build/%.o: src/%.c src/game.h
	@mkdir -p build
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

build/irx_sio2man.c: $(PS2SDK)/iop/irx/sio2man.irx
	@mkdir -p build
	$(BIN2C) $< $@ irx_sio2man

build/irx_padman.c: $(PS2SDK)/iop/irx/padman.irx
	@mkdir -p build
	$(BIN2C) $< $@ irx_padman

clean:
	rm -rf build $(EE_BIN)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
