KERNEL_SOURCE_VERSION ?= $(shell uname -r)
KERNEL_TREE ?= /lib/modules/$(KERNEL_SOURCE_VERSION)/build
UBBD_SRC := $(shell pwd)
KMODS_SRC := $(UBBD_SRC)/kmods
KTF_SRC := $(shell pwd)/unittests/ktf
EXTRA_CFLAGS += $(call cc-option,-Wno-tautological-compare) -Wall -Wmaybe-uninitialized -Werror
ifdef UBBD_DEBUG
EXTRA_CFLAGS += -g
endif
VERSION ?= $(shell cat VERSION)
UBBD_VERSION ?= ubbd-$(VERSION)
$(shell rm -rf include/ubbd_compat.h)
UBBDCONF_HEADER := include/ubbd_compat.h
OCFDIR = ocf/

UBBD_FLAGS = "-I /usr/include/libnl3/ -I$(UBBD_SRC)/libs3/inc -I $(UBBD_SRC)/include/ -I$(UBBD_SRC)/src/ocf/env/ -I$(UBBD_SRC)/src/ocf/ -L$(UBBD_SRC)/libs3/build/lib/ -ls3"

.DEFAULT_GOAL := all

$(UBBDCONF_HEADER):
	@> $@
	@echo $(CHECK_BUILD) compat-tests/have_sftp_fsync.c
	@if $(CC) compat-tests/have_sftp_fsync.c -lssh > /dev/null 2>&1; then echo "#define HAVE_SFTP_FSYNC 1"; else echo "/*#undefined HAVE_SFTP_FSYNC*/"; fi >> $@
	@>> $@

ubbdadm: $(UBBDCONF_HEADER)
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" UBBD_FLAGS=$(UBBD_FLAGS) $(MAKE) -C ubbdadm

lib: $(UBBDCONF_HEADER)
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" UBBD_FLAGS=$(UBBD_FLAGS) $(MAKE) -C lib/

backend: $(UBBDCONF_HEADER)
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" UBBD_FLAGS=$(UBBD_FLAGS) $(MAKE) -C backend

ubbdd: $(UBBDCONF_HEADER)
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" UBBD_FLAGS=$(UBBD_FLAGS) $(MAKE) -C ubbdd

all: $(UBBDCONF_HEADER)
	git submodule update --init --recursive
	@$(MAKE) -C ${OCFDIR} inc O=$(PWD)
	@$(MAKE) -C ${OCFDIR} src O=$(PWD)
	@$(MAKE) -C ${OCFDIR} env O=$(PWD) OCF_ENV=posix
	@$(MAKE) -C libs3/ clean
	@$(MAKE) -C libs3/
	@ln -s libs3.so.4 libs3/build/lib/libs3.so
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" UBBD_FLAGS=$(UBBD_FLAGS) $(MAKE) -C lib/
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" UBBD_FLAGS=$(UBBD_FLAGS) $(MAKE) -C ubbdadm
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" UBBD_FLAGS=$(UBBD_FLAGS) $(MAKE) -C ubbdd
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" UBBD_FLAGS=$(UBBD_FLAGS) $(MAKE) -C backend
	@echo
	@rm -rf kmods/compat.h
	cd kmods; KMODS_SRC=$(KMODS_SRC) UBBD_KMODS_UT="n" KTF_SRC=$(KTF_SRC) $(MAKE) -C $(KERNEL_TREE) M=$(PWD)/kmods modules V=0
	@echo "Compile completed."

kmod_ut: $(UBBDCONF_HEADER)
	@rm -rf kmods/compat.h
	cd kmods; KMODS_SRC=$(KMODS_SRC) UBBD_KMODS_UT="m" KTF_SRC=$(KTF_SRC) $(MAKE) -C $(KERNEL_TREE) M=$(PWD)/kmods modules V=0

ubbd_ut: $(UBBDCONF_HEADER)
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" UBBD_FLAGS=$(UBBD_FLAGS) $(MAKE) -C unittests

clean:
	$(MAKE) -C ubbdadm clean
	$(MAKE) -C ubbdd clean
	$(MAKE) -C backend clean
	$(MAKE) -C unittests clean
	$(MAKE) -C lib clean
	cd kmods; $(MAKE) clean
	rm -vf rhed/ubbd.spec

install:
	mkdir -p $(DESTDIR)/usr/bin
	mkdir -p $(DESTDIR)/usr/lib/ubbd/
	install etc/ld.so.conf.d/ubbd.conf $(DESTDIR)/etc/ld.so.conf.d/ubbd.conf
	install lib/libubbd.so $(DESTDIR)/usr/lib/ubbd/libubbd.so
	install libs3/build/lib/libs3.so.4 $(DESTDIR)/usr/lib/ubbd/libs3.so.4
	install ubbdadm/ubbdadm $(DESTDIR)/usr/bin/ubbdadm
	install ubbdd/ubbdd $(DESTDIR)/usr/bin/ubbdd
	install backend/ubbd-backend $(DESTDIR)/usr/bin/ubbd-backend
	cd kmods; KMODS_SRC=$(KMODS_SRC) UBBD_KMODS_UT="n" KTF_SRC=$(KTF_SRC) $(MAKE) -C $(KERNEL_TREE) M=$(PWD)/kmods modules_install V=0
	depmod -a
	ldconfig

uninstall:
	rm -vf $(DESTDIR)/usr/bin/ubbdadm
	rm -vf $(DESTDIR)/usr/bin/ubbdd
	rm -vf $(DESTDIR)/usr/bin/ubbd-backend
	rm -vrf $(DESTDIR)/usr/lib/ubbd/
	rm -vf $(DESTDIR)/etc/lib.so.conf.d/ubbd.conf
	rm -vf $(DESTDIR)/lib/modules/`uname -r`/extra/ubbd.ko

dist:
	git submodule update --init --recursive
	sed "s/@VERSION@/$(VERSION)/g" rhel/ubbd.spec.in > rhel/ubbd.spec
	cd /tmp && mkdir -p $(UBBD_VERSION) && \
	cp -rf $(UBBD_SRC)/{ubbdadm,ubbdd,backend,lib,include,doc,kmods,Makefile,ocf,libs3} $(UBBD_VERSION) && \
	tar --format=posix -chf - $(UBBD_VERSION) | gzip -c > $(UBBD_SRC)/$(UBBD_VERSION).tar.gz && \
	rm -rf $(UBBD_VERSION)
