include ../Makefile.inc

SUBDIR		= libc/
BUILD_DIR	= ../bin/$(SUBDIR)

# C source file list
C_FILES		= string.c \
			  stdlib.c \
			  stdlib_memory.c \
			  linkedlist.c \
			  ringbuffer.c

# Object files
OBJS    	:= $(C_FILES:.c=.o)
OBJS_BD		:= $(addprefix $(BUILD_DIR),$(OBJS))
OBJS_SD		:= $(addprefix $(SUBDIR),$(OBJS))

INCLUDES = -I"../include/" -I"../include/std/"

.PHONY: all 

all : $(OBJS_BD)

$(BUILD_DIR)%.o: %.c
	@echo Compiling file \"$<\"...
	@$(GCC) $(INCLUDES) -c "$<" -o "$@" $(CFLAGS)
	
obj-list:
	#Since I HAVE NO IDEA how to prefix this out coming string pieces with "bin/" prefix inside
	#the main Makefile. I guess I can do this here
	@echo $(addprefix bin/,$(OBJS_SD))
