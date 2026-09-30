ifndef PS5_PAYLOAD_SDK
    $(error PS5_PAYLOAD_SDK is undefined)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ELF := gl-test.elf

CFLAGS := -Wall -O2 $(shell $(PKG_CONFIG) --cflags sdl2)
# libOSMesa.so is loaded with dlopen and imports libc functions from this executable; the PS5 runtime
# linker only resolves symbols that the executable exports, so export them all.
LDLIBS := $(shell $(PKG_CONFIG) --libs sdl2) -Wl,--export-dynamic

all: $(ELF)

$(ELF): main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f $(ELF)
