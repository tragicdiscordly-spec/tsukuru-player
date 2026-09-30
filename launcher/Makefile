ifndef PS5_PAYLOAD_SDK
    $(error PS5_PAYLOAD_SDK is undefined)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ELF := launcher.elf

CFLAGS := -Wall -O2 -D_GNU_SOURCE $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_ttf)
LDLIBS := $(shell $(PKG_CONFIG) --libs --static sdl2 SDL2_ttf) -lbz2

all: $(ELF)

$(ELF): main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f $(ELF)
