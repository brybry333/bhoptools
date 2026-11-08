CXX      ?= g++
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-function
CXXFLAGS += -I../shared -std=c++14 -fno-strict-aliasing -ffp-contract=off -fno-math-errno

HEADERS := $(wildcard ../shared/*.h)
RELEASE_SIG_SRCS := ../shared/release_sig.cpp ../shared/vendor/monocypher/monocypher.c \
                    ../shared/vendor/monocypher/monocypher-ed25519.c ../shared/vendor/sha256/sha256.c

.PHONY: all test clean

all: release_sig_test

# g++ builds the vendored .c files as C++, the same as the client builds do.
release_sig_test: release_sig_test.cpp $(RELEASE_SIG_SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) $< $(RELEASE_SIG_SRCS) -o $@

# The workflows check a release with it before publishing.
check_sums: check_sums.cpp $(RELEASE_SIG_SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) $< $(RELEASE_SIG_SRCS) -o $@

test: release_sig_test
	./release_sig_test

clean:
	rm -f release_sig_test check_sums
