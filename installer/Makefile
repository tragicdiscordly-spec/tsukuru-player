ifndef PS5_PAYLOAD_SDK
    $(error PS5_PAYLOAD_SDK is undefined)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

CFLAGS  := -Wall -Werror
LDLIBS  := -lSceIpmi -lSceAppInstUtil

all: install.elf

install.elf: install.c ../tile/assets/param.json ../tile/assets/icon0.png ../tile/assets/launch.html
	$(CC) $(CFLAGS) -o $@ install.c $(LDLIBS)

clean:
	rm -f install.elf
