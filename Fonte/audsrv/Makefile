# audsrv do PS2SDK (commit b12f8af, o mesmo do ps2dev v2.0.0), compilado com
# o toolchain instalado. Única mudança no código: o volume do PCM em
# src/audsrv.c (veja LEIA-ME.txt desta pasta). Licença: LGPL-2 (Licencas/).
ifndef PS2SDK
$(error Set PS2SDK and PS2DEV to your installed ps2dev toolchain)
endif
IOP_BIN ?= audsrv.irx
IOP_OBJS_DIR ?= obj/
IOP_SRC_DIR := src/
IOP_INC_DIR := include/
IOP_INCS := -Isrc
IOP_DBGINFOFLAGS := -gdwarf-2 -gz
IOP_OBJS := audsrv.o upsamplers.o hw.o rpc_server.o rpc_client.o common.o cdrom.o imports.o exports.o adpcm.o
.PHONY: all
all: $(IOP_BIN)
include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
$(addprefix $(IOP_OBJS_DIR),$(IOP_OBJS)): $(wildcard src/*.h include/*.h) Makefile
