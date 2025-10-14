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


# CXX       = g++
# CXXFLAGS  = -Wall -O2 -std=c++17
# LDFLAGS   = -lX11 -pthread
# TARGET    = multitab
# SRC       = multitab_unicode.cpp
# OBJ       = $(SRC:.cpp=.o)

# all: $(TARGET)

# $(TARGET): $(OBJ)
# 	$(CXX) $(OBJ) -o $(TARGET) $(LDFLAGS)

# %.o: %.cpp
# 	$(CXX) $(CXXFLAGS) -c $< -o $@

# run: $(TARGET)
# 	LANG=en_US.UTF-8 ./$(TARGET)

# clean:
# 	rm -f $(TARGET) $(OBJ)
