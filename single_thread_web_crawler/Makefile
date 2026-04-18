# Makefile for lab1

CC = gcc 
CFLAGS_XML2 = $(shell xml2-config --cflags)
CFLAGS_CURL = $(shell curl-config --cflags)
CFLAGS = -Wall $(CFLAGS_XML2) $(CFLAGS_CURL) -std=gnu99 -g -DDEBUG1_
LD = gcc
LDFLAGS = -std=gnu99 -g 
LDLIBS_XML2 = $(shell xml2-config --libs)
LDLIBS_CURL = $(shell curl-config --libs)
LDLIBS = $(LDLIBS_XML2) $(LDLIBS_CURL) -lcurl

SRC_DIR =src
INC_DIR = include

# COMMON_SRCS = lab_png.c crc.c zutil.c
# COMMON_SRCS := $(addprefix $(SRC_DIR)/,$(COMMON_SRCS))
TARGET_SRCS = findpng3.c
TARGET_SRCS := $(addprefix $(SRC_DIR)/,$(TARGET_SRCS))

COMMON_OBJS = $(COMMON_SRCS:$(SRC_DIR)/%.c=%.o)
TARGET_OBJS = $(TARGET_SRCS:$(SRC_DIR)/%.c=%.o)

TARGETS =findpng3

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
