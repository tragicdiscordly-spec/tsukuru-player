ifndef PS5_PAYLOAD_SDK
    $(error PS5_PAYLOAD_SDK is undefined)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ELF := cwd-test.elf

all: $(ELF)

$(ELF): main.c
	$(CC) -Wall -O2 -o $@ $^

clean:
	rm -f $(ELF)
