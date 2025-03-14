# -------- Configuration begins

# Uncomment this line if you want a static build
#STATIC = true

# Build folder
OBJDIR = ./build

# Source files
# You may also want to add the file path to the search list
#   with `vpath` directive
C_SRC = 
CPP_SRC = 

# Executable file name
EXE = 

# Compilers
CC = 
CXX = 

# Compiler flags
CFLAGS = 
CXXFLAGS = 

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
endif

ifeq ($(OS), Windows_NT)
# MinGW
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
