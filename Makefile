CC = g++-10
CFLAGS = -g -Wall -Werror -Wextra -ffast-math

SRC = ./solution.cpp 
TARGET = solution

ifeq ($(DEBUG),1)
	CFLAGS += -O0 -DDEBUG
else
	CFLAGS += -O3 -DNDEBUG
endif

all:
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) 
