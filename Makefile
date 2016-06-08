# $Id: Makefile 5 2011-01-31 03:48:23Z henry_groover $
# Makefile for licut

OUTPUT:=../output
HOST_TARGET:=$(shell uname -m)-$(shell uname -s | tr 'L' 'l')
# Override TARGET for cross-compile
TARGET:=${HOST_TARGET}
TGT:=
ifeq (${TARGET},arm-linux)
TGT:=arm-linux-
endif
PACKAGE:=${OUTPUT}/licut-${TARGET}.tgz
SOURCES:=$(wildcard *.cpp)
BINDIR:=${TARGET}/bin
OBJDIR:=${TARGET}/obj
LIBDIR:=${TARGET}/lib
OBJS:=$(patsubst %.cpp,${OBJDIR}/%.o,${SOURCES})
GFLAGS_LIB:=libgflags_nothreads.a
LIBS:=${GFLAGS_LIB}
LIB_PATHS:=$(addprefix ${LIBDIR}/,${LIBS})
#LDFLAGS += -L${LIBDIR} $(addprefix -l,$(patsubst lib%,%,${LIBS}))
LDFLAGS += ${LIB_PATHS}
CFLAGS += -Igoogle-gflags/src


LICUT:=${TARGET}/bin/licut

all: ${PACKAGE}

${PACKAGE}: ${LICUT} ${OUTPUT} ${OUTPUT}/${TARGET}
	cp ${LICUT} ${OUTPUT}/${TARGET}; svn export --force ../doc ${OUTPUT}/${TARGET}/doc; cd ${OUTPUT}/${TARGET}; tar czf ../$(@F) *
	@ls -l $@

${OUTPUT} ${OUTPUT}/${TARGET} ${LIBDIR}:
	mkdir -p $@

clean:
	rm -f ${LICUT} ${OBJS} ${PACKAGE}

.PHONY: all clean

${LICUT}: ${OBJS} ${LIB_PATHS}
	@mkdir -p $(dir $@)
	${TGT}${CXX} -o $@ ${OBJS} ${LDFLAGS}
	cp $@ $@.debug
	${TGT}strip $@

${OBJDIR}/%.o: %.cpp
	@mkdir -p $(dir $@)
	${TGT}${CXX} ${CFLAGS} ${CPPFLAGS} -c -o $@ $<

ifneq (${SKIP_LIB},1)
${LIBDIR}/${GFLAGS_LIB}: google-gflags/.libs/${GFLAGS_LIB}
	cp -p -P google-gflags/.libs/*.so* google-gflags/.libs/*.a ${LIBDIR}/
	@touch $@
endif

google-gflags/.libs/${GFLAGS_LIB}: config-google-gflags-${TARGET} ${LIBDIR}
	make -C google-gflags

include google-gflags.mak

