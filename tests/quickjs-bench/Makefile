ifndef PS5_PAYLOAD_SDK
    $(error PS5_PAYLOAD_SDK is undefined)
endif
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ELF := quickjs-bench.elf
# js_pool_alloc.h lives in the Outsider tree (patches/outsider)
OUTSIDER_ENGINE ?= $(HOME)/easyrpg/mv/outsider/src/engine

all: $(ELF)

$(ELF): main.c
	$(CC) -Wall -O2 -I$(PS5_PAYLOAD_SDK)/target/user/homebrew/include -I$(OUTSIDER_ENGINE) -o $@ $^ \
	    -L$(PS5_PAYLOAD_SDK)/target/user/homebrew/lib -lqjs -lm -lpthread

clean:
	rm -f $(ELF)
