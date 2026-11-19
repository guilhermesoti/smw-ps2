.DEFAULT_GOAL := all
ifeq ($(strip $(PS2SDK)),)
$(error Set PS2SDK and PS2DEV to your installed ps2dev toolchain)
endif
GSKIT ?= $(PS2DEV)/gsKit
include $(PS2SDK)/samples/Makefile.pref
OUT := build
ENGINE := $(wildcard engine/src/*.c engine/src/snes/*.c)
PORT := ps2/main.c ps2/game.c ps2/files.c ps2/intro.c ps2/formato.c
DRIVERS := bdm bdmfs_fatfs usbd usbmass_bd sio2man padman mcman mcserv freesd audsrv
OBJECTS := $(addprefix $(OUT)/,$(ENGINE:.c=.o) $(PORT:.c=.o))
OBJECTS += $(addprefix $(OUT)/irx/,$(addsuffix .o,$(DRIVERS)))
CPPFLAGS := -D_EE -Iengine -I$(PS2SDK)/ee/include -I$(PS2SDK)/common/include -I$(PS2SDK)/ports/include -I$(GSKIT)/include -Icompat
CFLAGS := -std=gnu11 -O2 -G0 -fno-strict-aliasing -fwrapv -ffunction-sections -fdata-sections -MMD -MP
$(OUT)/ps2/%.o: CFLAGS += -Wall -Wextra -Wno-unknown-pragmas
LDFLAGS := -T$(PS2SDK)/ee/startup/linkfile -G0 -Wl,--gc-sections,-zmax-page-size=128 -L$(PS2SDK)/ee/lib -L$(PS2SDK)/ports/lib -L$(GSKIT)/lib
LDLIBS := -lgskit -ldmakit -lpatches -lpad -lmc -laudsrv -ldebug -lunzip -lz -lm
all: ../APPS/SMW/SMW.ELF
$(OUT)/game.elf: $(OBJECTS)
	$(EE_CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@
../APPS/SMW/SMW.ELF: $(OUT)/game.elf
	@mkdir -p $(@D)
	$(EE_STRIP) --strip-all -o $@ $<
$(OUT)/%.o: %.c
	@mkdir -p $(@D)
	$(EE_CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(OUT)/irx/%.c: $(PS2SDK)/iop/irx/%.irx
	@mkdir -p $(@D)
	$(PS2SDK)/bin/bin2c $< $@ $*_irx
# audsrv sai do código em audsrv/ (PS2SDK com o volume do PCM corrigido).
$(OUT)/iop/audsrv.irx: $(wildcard audsrv/src/* audsrv/include/*) audsrv/Makefile
	@mkdir -p $(@D)
	$(MAKE) -C audsrv IOP_BIN=../$@ IOP_OBJS_DIR=../$(OUT)/iop/obj/
$(OUT)/irx/audsrv.c: $(OUT)/iop/audsrv.irx
	@mkdir -p $(@D)
	$(PS2SDK)/bin/bin2c $< $@ audsrv_irx
$(OUT)/irx/%.o: $(OUT)/irx/%.c
	$(EE_CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
clean:
	rm -rf $(OUT)
.PHONY: all clean
-include $(OBJECTS:.o=.d)
