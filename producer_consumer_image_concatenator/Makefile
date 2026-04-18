# Makefile for lab1

CC = gcc
CFLAGS = -Wall -g -std=c99 -Iinclude
LD = gcc
LDFLAGS = -g
LIBS = -lz -lpthread
LDLIBS = -lcurl # "curl-config --libs" output 

SRC_DIR = src
INC_DIR = include

COMMON_SRCS = lab_png.c crc.c zutil.c
COMMON_SRCS := $(addprefix $(SRC_DIR)/,$(COMMON_SRCS))
TARGET_SRCS = pnginfo.c findpng.c catpng.c
TARGET_SRCS := $(addprefix $(SRC_DIR)/,$(TARGET_SRCS))

COMMON_OBJS = $(COMMON_SRCS:$(SRC_DIR)/%.c=%.o)
TARGET_OBJS = $(TARGET_SRCS:$(SRC_DIR)/%.c=%.o)

TARGETS = paster2

all: $(TARGETS)

$(TARGETS): %: %.o $(COMMON_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(LIBS)

%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.d: $(SRC_DIR)/%.c
	$(CC) -MM -MF $@ -I$(INC_DIR) $<

-include $(COMMON_OBJS:.o=.d) $(TARGET_OBJS:.o=.d)

.PHONY: clean
clean:
	rm -f *.o *.d $(TARGETS)
