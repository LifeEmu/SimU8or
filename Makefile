# -------- Configuration begins

# Uncomment this line if you want a static build
#STATIC = true

# Build folder
OBJDIR = ./build

# Source files
# You may also want to add the file path to the search list
#   with `vpath` directive
C_SRC = $(wildcard ./src/SFR/*.c) $(wildcard ./src/*.c)
CPP_SRC = 
vpath %.c ./src/SFR

# Executable file name
EXE = SimU8or

# Compilers
CC = gcc
CXX = g++

# Compiler flags
CFLAGS = -std=c99 -Wall -Wextra -pedantic -Oz
CXXFLAGS = -std=c++11 -Wall -Wextra -pedantic -Oz

# Linker flags
# If you have used C++ code, remember to add `-lstdc++` here.
LDFLAGS = 


# If your choice of UI library needs special flags,
# Please add them here.
ifdef STATIC

CFLAGS += 
CXXFLAGS += 
LDFLAGS += -static

else

CFLAGS += 
CXXFLAGS += 
LDFLAGS += 

endif


# -------- Configuration ends



# User object files
C_OBJ = $(addprefix $(OBJDIR)/,$(patsubst %.c,%.o,$(notdir $(C_SRC))))
CPP_OBJ = $(addprefix $(OBJDIR)/,$(patsubst %.cpp,%.o,$(notdir $(CPP_SRC))))

# SimU8 source files
SimU8_SRC = $(wildcard ./SimU8/src/*.c)
SimU8_OBJ = $(addprefix $(OBJDIR)/,$(patsubst %.c,%.o,$(notdir $(SimU8_SRC))))

# SimU8 C flags
SimU8_CFLAGS = -std=c99 -Wall -Wextra -pedantic -Oz

# Append include path so it could be visible to user programs
CFLAGS += -I./SimU8/src
CXXFLAGS += -I./SimU8/src

# vpath <pattern> <directive>
# Add search path for prerequisites
vpath %.c ./src:./SimU8/src
vpath %.h ./src:./SimU8/src

# Determine OS type
# Copied from Dear ImGui's examples
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S), Linux)
# Linux
OS = Linux

CFLAGS += `pkg-config --cflags sdl2`
ifdef STATIC
LDFLAGS += `pkg-config --static --libs sdl2` -lSDL2_ttf
else
LDFLAGS += `pkg-config --libs sdl2` -lSDL2_ttf
endif
endif

ifeq ($(OS), Windows_NT)
# MinGW
# SDL2 & SDL2-ttf needs different flags on my machine,
#   so I added linker flags here
CFLAGS += `pkg-config --cflags sdl2 sdl2_ttf`
ifdef STATIC
LDFLAGS += `pkg-config --static --libs sdl2 sdl2_ttf` -lstdc++ -mconsole
else
LDFLAGS += `pkg-config --libs sdl2 sdl2_ttf` -mconsole
endif
endif



# Targets

exe: $(SimU8_OBJ) $(C_OBJ) $(CPP_OBJ) | $(OBJDIR)
	@echo 
	@echo --------
	@echo Building \'$(EXE)\' for \'$(OS)\'...
	@echo --------
	@echo 
	$(CC) $^ $(LDFLAGS) -o $(EXE)


.PHONY: clean
clean:
	rm $(OBJDIR)/*


$(SimU8_OBJ): $(OBJDIR)/%.o : %.c | $(OBJDIR)
	$(CC) $^ $(SimU8_CFLAGS) -c -o $@

$(C_OBJ): $(OBJDIR)/%.o : %.c | $(OBJDIR)
	$(CC) $^ $(CFLAGS) -c -o $@

$(CPP_OBJ): $(OBJDIR)/%.o : %.cpp | $(OBJDIR)
	$(CXX) $^ $(CXXFLAGS) -c -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)
