CXX = g++
WINDRES = windres

INCLUDE = -Iinclude

SRC = $(wildcard src/*.cpp)

OBJ_DEBUG = $(SRC:src/%.cpp=build/debug/%.o)
OBJ_RELEASE = $(SRC:src/%.cpp=build/release/%.o)

RES_SRC = resources.rc
RES_DEBUG = build/debug/resources.res
RES_RELEASE = build/release/resources.res

LIBS = -lgdi32 -lcomctl32 -ldwmapi -luser32 -lpsapi -lshell32 \
       -lole32 -loleaut32 -lshlwapi -luuid -pthread -lgdiplus

CXXFLAGS_DEBUG = -std=c++17 -g -Wall -Wextra -D_DEBUG
CXXFLAGS_RELEASE = -std=c++17 -O3 -Wall -DNDEBUG

LDFLAGS_DEBUG =
LDFLAGS_RELEASE = -mwindows -static -s


debug: kinesis.exe
release: bin/kinesis.exe

kinesis.exe: $(OBJ_DEBUG) $(RES_DEBUG)
	$(CXX) $^ -o $@ $(LDFLAGS_DEBUG) $(LIBS)

bin/kinesis.exe: $(OBJ_RELEASE) $(RES_RELEASE) | bin
	$(CXX) $^ -o $@ $(LDFLAGS_RELEASE) $(LIBS)


build/debug/%.o: src/%.cpp | build/debug
	$(CXX) $(CXXFLAGS_DEBUG) $(INCLUDE) -c $< -o $@

build/release/%.o: src/%.cpp | build/release
	$(CXX) $(CXXFLAGS_RELEASE) $(INCLUDE) -c $< -o $@


build/debug/resources.res: $(RES_SRC) | build/debug
	$(WINDRES) $< -O coff -o $@

build/release/resources.res: $(RES_SRC) | build/release
	$(WINDRES) $< -O coff -o $@


build/debug:
	if not exist build mkdir build
	if not exist build\debug mkdir build\debug

build/release:
	if not exist build mkdir build
	if not exist build\release mkdir build\release

bin:
	if not exist bin mkdir bin

clean:
	if exist build rmdir /S /Q build
	if exist bin rmdir /S /Q bin
	del /Q kinesis.exe 2>NUL || exit 0