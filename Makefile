# Enrollment app — layered C project (v2.1)
# Targets GTK3 + libcurl (installed via pkg-config).

CC         ?= gcc
CFLAGS     ?= -O2 -Wall -Wextra -std=c11

GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 2>/dev/null)
GTK_LIBS   := $(shell pkg-config --libs   gtk+-3.0 2>/dev/null)
CURL_CFLAGS := $(shell pkg-config --cflags libcurl 2>/dev/null)
CURL_LIBS  := $(shell pkg-config --libs   libcurl 2>/dev/null)

ALL_CFLAGS = $(CFLAGS) -Isrc $(GTK_CFLAGS) $(CURL_CFLAGS)

APP_SRC  = src/main.c src/enrollment.c src/subjects.c src/sheets_gateway.c \
           src/applog.c
LIB_SRC  = src/enrollment.c src/subjects.c src/sheets_gateway.c

# On Windows (MSYS2) the window binary gets the GUI subsystem: double-
# clicking it opens the window directly, with no console window flashing.
ifeq ($(OS),Windows_NT)
GUI_LDFLAGS := -mwindows
endif

all: enrollment-app test_gateway test_unit test_subjects

# The desktop window (GUI)
enrollment-app: $(APP_SRC)
	$(CC) $(ALL_CFLAGS) -o $@ $(APP_SRC) $(GTK_LIBS) $(CURL_LIBS) -pthread $(GUI_LDFLAGS)

# CLI smoke test for the Sheets pipeline (no GUI needed)
test_gateway: src/test_gateway.c $(LIB_SRC)
	$(CC) $(ALL_CFLAGS) -o $@ src/test_gateway.c $(LIB_SRC) $(CURL_LIBS) -pthread

# Dependency-free unit tests
test_unit: tests/test_enrollment.c src/enrollment.c src/subjects.c
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_enrollment.c \
		src/enrollment.c src/subjects.c

test_subjects: tests/test_subjects.c src/subjects.c
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_subjects.c src/subjects.c

# Build and run all unit tests
test: test_unit test_subjects
	./test_unit
	./test_subjects

# Production packaging (run this in the MSYS2 UCRT64/MINGW64 shell on
# Windows): builds, tests, then assembles dist/EnrollmentApp with every
# required DLL and GTK runtime file, and zips it for distribution.
portable:
	bash deploy/deploy-portable.sh

clean:
	rm -f enrollment-app enrollment-app.exe test_gateway test_unit test_subjects
	rm -rf dist

.PHONY: all test portable clean
