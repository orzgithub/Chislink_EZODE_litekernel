DEVKITPRO ?= /opt/devkitpro
DEVKITARM := $(DEVKITPRO)/devkitARM
LIBGBA    := $(DEVKITPRO)/libgba

CC      := $(DEVKITARM)/bin/arm-none-eabi-gcc
OBJCOPY := $(DEVKITARM)/bin/arm-none-eabi-objcopy
GBAFIX  ?= /opt/devkitpro/tools/bin/gbafix

CL := ./chislinksdk

CFLAGS := -mthumb -mthumb-interwork -mcpu=arm7tdmi -Os \
          -ffunction-sections -fdata-sections -fno-builtin -fno-common \
          -Wall -Wextra -Wno-unused-parameter \
          -I$(CL)/gba/runtime/include -I$(CL)/gba/sdk/include \
          -I$(CL)/common/chislink_proto/include -I$(LIBGBA)/include \
          -Isrc

LDFLAGS := -specs=gba_mb.specs -Wl,--gc-sections -L$(LIBGBA)/lib

SRCS := src/main.c src/browser.c   src/iobuf.c src/save.c src/savemode.c src/ui.c src/loader.c \
        src/font_lookup.c \
        src/lfont.c \
        $(CL)/gba/runtime/src/dma.c \
        $(CL)/gba/runtime/src/hw.c \
        $(CL)/gba/runtime/src/sio.c \
        $(CL)/gba/runtime/src/text.c \
        $(CL)/gba/runtime/src/video.c \
        $(CL)/gba/sdk/src/client.c \
        $(CL)/gba/sdk/src/copy.c \
        $(CL)/gba/sdk/src/file.c \
        $(CL)/gba/sdk/src/gba_sio_transport.c \
        $(CL)/gba/sdk/src/storage_client.c \
        $(CL)/gba/sdk/src/wire.c \
        $(CL)/common/chislink_proto/src/proto.c

all: litekernel.mb.gba

litekernel.mb.elf: $(SRCS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) -lgba -lgcc

litekernel.mb.gba: litekernel.mb.elf
	$(OBJCOPY) -O binary $< $@
	$(GBAFIX) $@ -t'ODE LITE' -cLKRN -m01
	@ls -l $@

clean:
	rm -f litekernel.mb.elf litekernel.mb.gba

.PHONY: all clean
