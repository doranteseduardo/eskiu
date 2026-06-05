# INE QR Decoder — Makefile
#
# Fully self-contained: no dependency on ine-qr-c or any other project.
# External dependencies (system libraries only):
#
#   OpenSSL   — AES-256-CBC + RSA/BN operations (libcrypto)
#   zxing-cpp — QR code detection
#   CoreGraphics / CoreFoundation / ImageIO — HEIC/JPEG image loading (macOS)

ESKIUC   := ../build/eskiuc
CXX      := c++
CC       := cc
CXXFLAGS := -std=c++17 -O2
CFLAGS   := -O2

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

all: qr_decoder

# Eskiu → object file (includes crypto.esk and output.esk)
main.o: main.esk types.esk extern.esk pipeline.esk crypto.esk output.esk
	$(ESKIUC) main.esk -o main.o

# C/C++ sources — QR extraction only (crypto + output are in Eskiu)
qr_extract.o: qr_extract.c
	$(CC) $(CFLAGS) $(INCLUDES) -c qr_extract.c -o qr_extract.o

qr_extract_impl.o: qr_extract_impl.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c qr_extract_impl.cpp -o qr_extract_impl.o

# Final binary — only links system libraries
qr_decoder: main.o qr_extract.o qr_extract_impl.o
	clang $^ $(LIBS) -o $@

clean:
	rm -f *.o qr_decoder /tmp/qr_decoded.json /tmp/qr_decoded.webp
