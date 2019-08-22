SRC_FILES = $(wildcard src/*.cpp)
OBJ_FILES = $(SRC_FILES:src/%.cpp=build/%.o)
DEP_FILES = $(OBJ_FILES:.o=.d)

CPPFLAGS += -std=c++17 -O2
CPPFLAGS += -I ./src/
CPPFLAGS += -MMD -MP

UWS       = ./lib/uWebSockets
JSON      = ./lib/json

CPPFLAGS += -I ./src/
CPPFLAGS += -I $(UWS)/src/
CPPFLAGS += -I $(JSON)/include/

TARGET    = libnaga.a

.PHONY: all clean dirs

all: dirs $(TARGET)

dirs:
	mkdir -p build

$(TARGET): $(OBJ_FILES)
	$(AR) rcs $@ $^

build/%.o: src/%.cpp
	$(CXX) $(CPPFLAGS) -c -o $@ $<

clean:
	- $(RM) $(TARGET) $(OBJ_FILES) $(DEP_FILES)

-include $(DEP_FILES)
