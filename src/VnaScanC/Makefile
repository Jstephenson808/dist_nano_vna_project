CC=clang
CFLAGS=-Wall -Werror

CLEANUP = rm -f
MKDIR = mkdir -p

ROOT_DIR = ../..
TEST_DIR = ${ROOT_DIR}/test/TestVnaScanC
UNITY_DIR = ${ROOT_DIR}/tools/Unity

UNITY_SOURCE = ${UNITY_DIR}/unity.c
INC_DIRS=-I./ -I$(UNITY_DIR)
SYMBOLS=

MULTI_NAME = VnaScanMultithreaded
MULTI_LINK = -lpthread -lm
MULTI_TEST_NAME = ${TEST_DIR}/Test${MULTI_NAME}
MULTI_TEST_SRC_FILES = ${UNITY_SOURCE} ${MULTI_NAME}.c ${MULTI_TEST_NAME}.c

all: scan scan-multithreaded scan-multithreaded-test

scan:
	$(CC) $(CFLAGS) VnaScan.c -o VnaScan 

scan-multithreaded:
	$(CC) $(CFLAGS) ${MULTI_NAME}.c ${MULTI_NAME}Main.c -o ${MULTI_NAME} ${MULTI_LINK}

scan-parser:
	$(CC) $(CFLAGS) ${MULTI_NAME}.c VnaCommandParser.c -o VnaCommandParser ${MULTI_LINK}

scan-multithreaded-test:
	${CC} ${CFLAGS} ${INC_DIRS} ${SYMBOLS} ${MULTI_TEST_SRC_FILES} -o ${MULTI_TEST_NAME} ${MULTI_LINK}
	- ./${MULTI_TEST_NAME}

debugging-symbols:
	${CC} ${CFLAGS} ${INC_DIRS} ${SYMBOLS} ${MULTI_TEST_SRC_FILES} -o ${MULTI_TEST_NAME} -g ${MULTI_LINK}

clean:
	${CLEANUP} VnaScan ${MULTI_NAME} ${MULTI_TEST_NAME}
