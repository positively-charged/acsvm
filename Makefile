# acsvm Makefile

EXE=acsvm
ROOT_BUILD_DIR=build
ifeq ($(shell uname -m),x86_64)
	BUILD_DIR=$(ROOT_BUILD_DIR)/x64
else
	BUILD_DIR=$(ROOT_BUILD_DIR)/x86
endif
COPIED_EXE=$(dir $(realpath Makefile))$(EXE)

CC=gcc
INCLUDE=-Isrc -I src/
OPTIONS=-Wall -Werror -Wno-unused -std=c99 -pedantic -Wstrict-aliasing \
	-Wstrict-aliasing=2 -Wmissing-field-initializers -D_BSD_SOURCE \
	-D_DEFAULT_SOURCE $(INCLUDE) -g

OBJECTS=\
	$(BUILD_DIR)/main.o \
	$(BUILD_DIR)/common/str.o \
	$(BUILD_DIR)/common/list.o \
	$(BUILD_DIR)/common/fs.o \
	$(BUILD_DIR)/common/mem.o \
	$(BUILD_DIR)/common/vector.o \
	$(BUILD_DIR)/load.o \
	$(BUILD_DIR)/instructions.o \
	$(BUILD_DIR)/aspec.o \
	$(BUILD_DIR)/ext.o \
	$(BUILD_DIR)/vm.o \
	$(BUILD_DIR)/debug.o

acsvm: $(OBJECTS)
	gcc -o acsvm $(OBJECTS)

$(BUILD_DIR)/main.o: \
	src/main.c \
	src/common/misc.h \
	src/common/mem.h \
	src/common/str.h \
	src/common/list.h \
	src/vm.h
	gcc $(OPTIONS) -c -o $@ $<

$(BUILD_DIR)/common/str.o: \
	src/common/str.c \
	src/common/misc.h \
	src/common/mem.h \
	src/common/str.h
	gcc $(OPTIONS) -c -o $@ $<

$(BUILD_DIR)/common/list.o: \
	src/common/list.c \
	src/common/misc.h \
	src/common/mem.h \
	src/common/list.h
	gcc $(OPTIONS) -c -o $@ $<

$(BUILD_DIR)/common/fs.o: \
	src/common/fs.c \
	src/common/misc.h \
	src/common/mem.h \
	src/common/str.h \
	src/common/fs.h
	gcc $(OPTIONS) -c -o $@ $<

$(BUILD_DIR)/common/mem.o: \
	src/common/mem.c \
	src/common/misc.h \
	src/common/list.h \
	src/common/mem.h
	gcc $(OPTIONS) -c -o $@ $<

$(BUILD_DIR)/common/vector.o: \
	src/common/vector.c \
	src/common/misc.h \
	src/common/mem.h \
	src/common/vector.h
	gcc $(OPTIONS) -c -o $@ $<

$(BUILD_DIR)/load.o: \
	src/load.c \
	src/common/misc.h \
	src/common/mem.h \
	src/common/str.h \
	src/common/list.h \
	src/vm.h
	gcc $(OPTIONS) -c -o $@ $<

$(BUILD_DIR)/instructions.o: \
	src/instructions.c \
	src/common/misc.h \
	src/common/mem.h \
	src/common/str.h \
	src/common/list.h \
	src/vm.h
	gcc $(OPTIONS) -c -o $@ $<
	gcc $(OPTIONS) -c -o $@ $<

$(BUILD_DIR)/aspec.o: \
	src/aspec.c \
	src/common/misc.h \
	src/common/mem.h \
	src/common/str.h \
	src/common/list.h \
	src/vm.h
	gcc $(OPTIONS) -c -o $@ $<
	gcc $(OPTIONS) -c -o $@ $<

$(BUILD_DIR)/ext.o: \
	src/ext.c \
	src/common/misc.h \
	src/common/mem.h \
	src/common/str.h \
	src/common/list.h \
	src/vm.h
	gcc $(OPTIONS) -c -o $@ $<

$(BUILD_DIR)/vm.o: \
	src/vm.c \
	src/common/misc.h \
	src/common/mem.h \
	src/common/str.h \
	src/common/list.h \
	src/vm.h \
	src/pcode.h \
	src/debug.h
	gcc $(OPTIONS) -c -o $@ $<

$(BUILD_DIR)/debug.o: \
	src/debug.c \
	src/common/misc.h \
	src/common/list.h \
	src/common/str.h \
	src/vm.h \
	src/debug.h
	gcc $(OPTIONS) -c -o $@ $<

