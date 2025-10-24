# Makefile

PROG = termgui
SRC  = $(PROG).cpp
OBJ  = $(SRC:.cpp=.o)

CC     = g++
CFLAGS = -Wall -Wextra -O0
INCS   = -I/usr/include
LIBS   = -lX11

# Default target
all: $(PROG)

$(PROG): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LIBS)

%.o: %.cpp
	$(CC) -c $< $(CFLAGS) $(INCS)

clean:
	rm -f $(OBJ) $(PROG)

.PHONY: all clean