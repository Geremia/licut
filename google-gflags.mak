# $Id: google-gflags.mak 4 2011-01-30 23:03:16Z henry_groover $
#
# Wrapper makefile to checkout, configure and build google-gflags

ifeq (${TARGET},)
$(error TARGET not defined - should default to HOST_TARGET)
endif

ifeq (${HOST_TARGET},)
$(error HOST_TARGET not defined - should default to uname -s | tr 'L' 'l')
endif

checkout-google-gflags:
	@echo "Checking to make sure google-gflags directory doesn't already exist..."
	@test -e google-gflags && { echo "google-gflags already exists - use update-google-gflags to update"; exit 1; }
	@echo "Attempting read-only checkout from subversion..."
	svn co http://google-gflags.googlecode.com/svn/trunk/ google-gflags
	@echo "Use update-google-gflags to update"

update-google-gflags:
	@test -d google-gflags || { echo "google-gflags not found; use checkout-google-gflags for initial checkout"; exit 1; }
	cd google-gflags ; svn info | awk '/^Revision:/ {print $$2;}' > ../.google-gflags-oldrev; svn up; svn info | awk '/^Revision:/ {print $$2;}' > ../.google-gflags-newrev
	@if test $$(cat .google-gflags-newrev) -gt $$(cat .google-gflags-oldrev); then echo "Update detected; forcing reconfigure on next build"; rm -f config-google-gflags-*; else echo "No change in svn revision"; fi

config-google-gflags-${TARGET}:

	-rm -f config-google-gflags-*
	-${MAKE} -C google-gflags clean
ifeq (${TARGET},${HOST_TARGET})
	cd google-gflags; ./configure
else
	@echo "Configuring for cross-compilation to ${TARGET}"
	cd google-gflags; ./configure --host=${TARGET}
endif
	touch $@

