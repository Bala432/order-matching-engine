CXX := g++
CXXFLAGS := -std=gnu++2c -O2 -Iinclude \
            -Igoogletest/googletest/include -Igoogletest/googlemock/include \
            -Igoogletest/googletest -pthread

GTEST_SRCS := googletest/googletest/src/gtest-all.cc googletest/googletest/src/gtest_main.cc
SRC := src/Orderbook.cpp
TESTS := tests/test_load.cpp
OUT := test_ome.exe

.PHONY: all test perf clean

all: $(OUT)

$(OUT): $(SRC) $(TESTS) $(GTEST_SRCS)
	$(CXX) $(CXXFLAGS) $(TESTS) $(SRC) $(GTEST_SRCS) -o $(OUT)

test: $(OUT)
	./$(OUT) --gtest_color=yes

# Perf target: builds the load binary with -O3 -march=native and separate output.
perf: CXXFLAGS += -O3 -march=native -flto
perf: TESTS += tests/test_load.cpp
perf: OUT := test_ome_perf.exe
perf: all
	./$(OUT)

clean:
	-del /Q $(OUT) 2>nul || rm -f $(OUT)
