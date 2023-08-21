#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

include $(DEVKITPRO)/libnx/switch_rules

ifeq ($(OS),Windows_NT)
MAKEPKG	:=	makepkg
else
ifeq (,$(shell which makepkg))
MAKEPKG	:=	dkp-makepkg
else
MAKEPKG	:=	makepkg
endif
endif

all: mpv nsp

.PHONY: harfbuzz
harfbuzz:
	@echo Installing harfbuzz
	@cd harfbuzz; $(MAKEPKG) -c -C -f -i -s --noconfirm; cd ..

.PHONY: libass
libass: harfbuzz
	@echo Installing libass
	@cd libass; $(MAKEPKG) -c -C -f -i -s --noconfirm; cd ..

.PHONY: dav1d
dav1d:
	@echo Installing dav1d
	@cd dav1d; $(MAKEPKG) -c -C -f -i -s --noconfirm; cd ..

.PHONY: ffmpeg
ffmpeg: libass dav1d
	@echo Installing ffmpeg
	@cd ffmpeg; $(MAKEPKG) -c -C -f -i -s --noconfirm; cd ..

.PHONY: mpv
mpv: ffmpeg
	@echo Installing libmpv
	@cd libmpv; $(MAKEPKG) -c -C -f -i -s --noconfirm; cd ..

.PHONY: nsp
nsp:
	@cd nspmini; $(MAKEPKG) -c -C -f -i -s --noconfirm; cd ..
