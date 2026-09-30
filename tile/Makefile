ifndef PS5_PAYLOAD_SDK
    $(error PS5_PAYLOAD_SDK is undefined)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

CFLAGS  := -Wall -Werror
LDLIBS  := -lSceIpmi -lSceAppInstUtil

all: install-tile.elf uninstall-tile.elf

install-tile.elf: install-tile.c assets/param.json assets/icon0.png assets/launch.html
	$(CC) $(CFLAGS) -o $@ install-tile.c $(LDLIBS)

uninstall-tile.elf: install-tile.c assets/param.json assets/icon0.png assets/launch.html
	$(CC) $(CFLAGS) -DUNINSTALL -o $@ install-tile.c $(LDLIBS)

clean:
	rm -f install-tile.elf uninstall-tile.elf
