TARGET_REL = build/PhantomD.exe
TARGET_DBG = build/PhantomD_d.exe

CC      = gcc
WINDRES = windres

SRC_DIR = src
INC_DIR = include
RES_DIR = res
BLD_DIR = build

SRCS = $(SRC_DIR)/main.c     \
       $(SRC_DIR)/gui.c      \
       $(SRC_DIR)/process.c  \
       $(SRC_DIR)/memory.c   \
       $(SRC_DIR)/hexdump.c  \
       $(SRC_DIR)/utils.c

OBJS_REL = $(patsubst $(SRC_DIR)/%.c, $(BLD_DIR)/%.o,      $(SRCS))
OBJS_DBG = $(patsubst $(SRC_DIR)/%.c, $(BLD_DIR)/%_d.o,    $(SRCS))

CFLAGS_COMMON = \
    -Wall -Wextra -Wno-unused-parameter -Wno-unknown-pragmas \
    -std=c11 \
    -I$(INC_DIR) \
    -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
    -DGDIPVER=0x0110 \
    -D_CRT_SECURE_NO_WARNINGS \
    -mwindows

CFLAGS_REL = $(CFLAGS_COMMON) -O2 -DNDEBUG
CFLAGS_DBG = $(CFLAGS_COMMON) -Og -g -DDEBUG

LDFLAGS = -mwindows
LIBS    = -lgdiplus -lcomctl32 -lcomdlg32 -lmsimg32 \
          -lshell32 -luser32 -lgdi32 -lkernel32

RES_OBJ = $(BLD_DIR)/resource.o

.PHONY: all release debug clean help

all: release

release: $(BLD_DIR) $(TARGET_REL)
	@echo [PhantomD] Release build complete: $(TARGET_REL)

$(TARGET_REL): $(OBJS_REL) $(RES_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(BLD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS_REL) -c -o $@ $<

debug: $(BLD_DIR) $(TARGET_DBG)
	@echo [PhantomD] Debug build complete: $(TARGET_DBG)

$(TARGET_DBG): $(OBJS_DBG) $(RES_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(BLD_DIR)/%_d.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS_DBG) -c -o $@ $<

$(RES_OBJ): $(RES_DIR)/resource.rc $(RES_DIR)/phantomd.ico
	$(WINDRES) --include-dir $(RES_DIR) $< -O coff -o $@

$(BLD_DIR):
	mkdir -p $(BLD_DIR)

clean:
	rm -f $(BLD_DIR)/*.o $(TARGET_REL) $(TARGET_DBG)

help:
	@echo "PhantomD build targets:"
	@echo "  make            release build  -> build/PhantomD.exe"
	@echo "  make debug      debug build    -> build/PhantomD_d.exe"
	@echo "  make clean      remove artefacts"
