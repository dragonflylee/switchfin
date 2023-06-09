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

all: ffmpeg mpv 

.PHONY: mpv
mpv:
	@echo Installing libmpv
	@cd libmpv; $(MAKEPKG) -c -C -f -i -s --noconfirm; cd ..

.PHONY: ffmpeg
ffmpeg:
	@echo Installing ffmpeg
	@cd ffmpeg; $(MAKEPKG) -c -C -f -i -s --noconfirm; cd ..