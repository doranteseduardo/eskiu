# INE QR Decoder — build instructions
#
# Prerequisites (macOS):
#   brew install llvm zxing-cpp openssl
#
# Prerequisites (Ubuntu):
#   apt install llvm-dev libssl-dev
#   # zxing-cpp: build from source or apt install libzxing-cpp-dev

ESKIUC    := ../build/eskiuc
CXX       := c++
CC        := cc
CXXFLAGS  := -std=c++17 -O2
CFLAGS    := -O2

# Detect platform
UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
    OPENSSL_PREFIX := $(shell brew --prefix openssl)
    ZXING_PREFIX   := $(shell brew --prefix zxing-cpp)
    INCLUDES := -I$(OPENSSL_PREFIX)/include -I$(ZXING_PREFIX)/include
    LIBS     := -L$(OPENSSL_PREFIX)/lib -lcrypto \
                -L$(ZXING_PREFIX)/lib -lZXing \
                -framework CoreFoundation \
                -framework CoreGraphics \
                -framework ImageIO \
                -lstdc++
else
    INCLUDES := -I/usr/include/openssl
    LIBS     := -lcrypto -lZXing -lstdc++
endif

.PHONY: all clean

all: ine_decoder

# Compile Eskiu source → object file
main.o: main.esk types.esk extern.esk stage1_qr.esk stage2_crypto.esk stage3_output.esk
	$(ESKIUC) main.esk -o main.o

# Compile C wrapper
qr_extract.o: qr_extract.c
	$(CC) $(CFLAGS) $(INCLUDES) -c qr_extract.c -o qr_extract.o

# Compile C++ zxing-cpp implementation
qr_extract_impl.o: qr_extract_impl.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c qr_extract_impl.cpp -o qr_extract_impl.o

# Link everything
ine_decoder: main.o qr_extract.o qr_extract_impl.o
	clang main.o qr_extract.o qr_extract_impl.o $(LIBS) -o ine_decoder

clean:
	rm -f *.o ine_decoder output.json output.webp
