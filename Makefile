TARGET = tick_feeder_binance
FUNDING_TARGET = funding_fetcher

CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread
CXXFLAGS += -Isrc -Itrcache -Itrcache/src/include -Ithird_party

TRCACHE_DIR = trcache
TRCACHE_LIB = $(TRCACHE_DIR)/libtrcache.a

LDFLAGS = -L$(TRCACHE_DIR) -ltrcache -lcurl -lpthread
FUNDING_LDFLAGS = -lcurl -lpthread

SRCS = src/main.cpp \
       src/config.cpp \
       src/engine.cpp \
       src/output_writer.cpp \
       src/downloader.cpp \
       src/decompressor.cpp \
       src/csv_parser.cpp \
       src/metadata.cpp

FUNDING_SRCS = src/funding_main.cpp \
               src/config.cpp \
               src/funding.cpp

OBJS = $(SRCS:.cpp=.o)
FUNDING_OBJS = $(FUNDING_SRCS:.cpp=.o)

.PHONY: all clean trcache

all: trcache $(TARGET) $(FUNDING_TARGET)

trcache:
	$(MAKE) -C $(TRCACHE_DIR)

$(TARGET): $(OBJS) $(TRCACHE_LIB)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(FUNDING_TARGET): $(FUNDING_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(FUNDING_OBJS) $(FUNDING_LDFLAGS)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(FUNDING_OBJS) $(TARGET) $(FUNDING_TARGET)
	$(MAKE) -C $(TRCACHE_DIR) clean

.PHONY: all clean trcache
