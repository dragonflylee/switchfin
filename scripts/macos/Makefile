OSX_ARCH ?= x86_64
PREFIX ?= $(CURDIR)/dylib/$(OSX_ARCH)
TMPDIR ?= $(CURDIR)/src

export CMAKE_PREFIX_PATH=$(PREFIX)
export DYLD_LIBRARY_PATH=$(PREFIX)/lib
export PKG_CONFIG_LIBDIR=$(PREFIX)/lib/pkgconfig

ifeq ($(OSX_ARCH),arm64)
	OSX_TARGET=11.0
else ifeq ($(OSX_ARCH),x86_64)
	OSX_TARGET=10.15
endif

all: download build

build: mpv curl mupdf glfw

clean:
	@rm -rf build

download:
	curl --retry 5 -sSL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2 | tar jxf - -C $(TMPDIR)
	curl --retry 5 -sSL https://curl.se/download/curl-8.16.0.tar.xz | tar Jxf - -C $(TMPDIR)
	curl --retry 5 -sSL https://github.com/webmproject/libwebp/archive/v1.6.0.tar.gz | tar zxf - -C $(TMPDIR)
	curl --retry 5 -sSL https://github.com/dragonflylee/glfw/archive/switchfin.tar.gz | tar zxf - -C $(TMPDIR)
	curl --retry 5 -sSL https://casper.mupdf.com/downloads/archive/mupdf-1.24.11-source.tar.lz | tar --lzip -xf - -C $(TMPDIR)
	curl --retry 5 -sSL https://www.freedesktop.org/software/uchardet/releases/uchardet-0.0.8.tar.xz | tar Jxf - -C $(TMPDIR)
	curl --retry 5 -sSL https://downloads.sourceforge.net/project/freetype/freetype2/2.14.3/freetype-2.14.3.tar.xz | tar Jxf - -C $(TMPDIR)
	curl --retry 5 -sSL https://github.com/harfbuzz/harfbuzz/releases/download/14.3.1/harfbuzz-14.3.1.tar.xz | tar Jxf - -C $(TMPDIR)
	curl --retry 5 -sSL https://github.com/fribidi/fribidi/releases/download/v1.0.16/fribidi-1.0.16.tar.xz | tar Jxf - -C $(TMPDIR)
	curl --retry 5 -sSL https://github.com/libass/libass/releases/download/0.17.5/libass-0.17.5.tar.xz | tar Jxf - -C $(TMPDIR)
	curl --retry 5 -sSL https://downloads.videolan.org/pub/videolan/dav1d/1.5.4/dav1d-1.5.4.tar.xz | tar Jxf - -C $(TMPDIR)
	curl --retry 5 -sSL https://ffmpeg.org/releases/ffmpeg-9.0.1.tar.xz | tar Jxf - -C $(TMPDIR)
	curl --retry 5 -sSL https://github.com/mpv-player/mpv/archive/v0.41.0.tar.gz | tar zxf - -C $(TMPDIR)
	git clone https://github.com/haasn/libplacebo.git -b v7.360.1 --depth 1 --recurse-submodules $(TMPDIR)/libplacebo
	patch -d $(TMPDIR)/mpv-0.41.0 -Nbp1 -i $(CURDIR)/mpv.patch

curl:
	cmake -B build/curl -S $(TMPDIR)/curl-8.16.0 -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_OSX_ARCHITECTURES=$(OSX_ARCH) -DCMAKE_OSX_DEPLOYMENT_TARGET=$(OSX_TARGET) \
		-DCMAKE_INSTALL_PREFIX=$(PREFIX) -DCMAKE_INSTALL_NAME_DIR=$(PREFIX)/lib \
		-DHTTP_ONLY=ON -DCURL_DISABLE_PROGRESS_METER=ON -DCURL_USE_MBEDTLS=ON \
		-DBUILD_SHARED_LIBS=ON -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF \
		-DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF \
		-DUSE_LIBIDN2=OFF -DUSE_NGHTTP2=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF \
		-DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBPSL=OFF
	cmake --build build/curl
	cmake --install build/curl

glfw:
	cmake -P $(TMPDIR)/glfw-switchfin/CMake/GenerateMappings.cmake \
		$(TMPDIR)/glfw-switchfin/src/mappings.h.in \
		$(TMPDIR)/glfw-switchfin/src/mappings.h
	cmake -B build/glfw -S $(TMPDIR)/glfw-switchfin -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_OSX_ARCHITECTURES=$(OSX_ARCH) -DCMAKE_OSX_DEPLOYMENT_TARGET=$(OSX_TARGET) \
		-DCMAKE_INSTALL_PREFIX=$(PREFIX) -DCMAKE_INSTALL_NAME_DIR=$(PREFIX)/lib \
		-DBUILD_SHARED_LIBS=ON -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF \
		-DGLFW_BUILD_DOCS=OFF
	cmake --build build/glfw
	cmake --install build/glfw

uchardet:
	cmake -B build/uchardet -S $(TMPDIR)/uchardet-0.0.8 -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_OSX_ARCHITECTURES=$(OSX_ARCH) -DCMAKE_OSX_DEPLOYMENT_TARGET=$(OSX_TARGET) \
		-DCMAKE_INSTALL_PREFIX=$(PREFIX) -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
		-DBUILD_SHARED_LIBS=OFF -DBUILD_BINARY=OFF
	cmake --build build/uchardet
	cmake --install build/uchardet

mbedtls:
	cmake -B build/mbedtls -S $(TMPDIR)/mbedtls-3.6.7 -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_OSX_ARCHITECTURES=$(OSX_ARCH) -DCMAKE_OSX_DEPLOYMENT_TARGET=$(OSX_TARGET) \
		-DCMAKE_INSTALL_PREFIX=$(PREFIX) -DCMAKE_INSTALL_NAME_DIR=$(PREFIX)/lib \
		-DUSE_SHARED_MBEDTLS_LIBRARY=ON -DUSE_STATIC_MBEDTLS_LIBRARY=OFF \
		-DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF
	cmake --build build/mbedtls
	cmake --install build/mbedtls

libwebp:
	cmake -B build/libwebp -S $(TMPDIR)/libwebp-1.6.0 -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_OSX_ARCHITECTURES=$(OSX_ARCH) -DCMAKE_OSX_DEPLOYMENT_TARGET=$(OSX_TARGET) \
		-DCMAKE_INSTALL_PREFIX=$(PREFIX) -DCMAKE_INSTALL_NAME_DIR=$(PREFIX)/lib \
		-DWEBP_BUILD_EXTRAS=OFF -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF \
		-DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF \
		-DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_LIBWEBPMUX=OFF -DBUILD_SHARED_LIBS=ON
	cmake --build build/libwebp
	cmake --install build/libwebp

freetype:
	meson setup build/freetype $(TMPDIR)/freetype-2.14.3 --default-library=shared \
		--cross-file "$(CURDIR)/$(OSX_ARCH).meson" --prefix=$(PREFIX) \
		-Dharfbuzz=disabled -Dbrotli=disabled -Dzlib=system
	meson compile -C build/freetype
	meson install -C build/freetype

harfbuzz: freetype
	meson setup build/harfbuzz $(TMPDIR)/harfbuzz-14.3.1 \
		--cross-file "$(CURDIR)/$(OSX_ARCH).meson" --prefix=$(PREFIX) \
		-Dtests=disabled -Ddocs=disabled -Dicu=disabled -Dcairo=disabled \
		-Dglib=disabled -Dgobject=disabled -Dbenchmark=disabled -Dutilities=disabled
	meson compile -C build/harfbuzz
	meson install -C build/harfbuzz

fribidi:
	meson setup build/fribidi $(TMPDIR)/fribidi-1.0.16 \
		--cross-file "$(CURDIR)/$(OSX_ARCH).meson" --prefix=$(PREFIX) \
		-Dbin=false -Ddocs=false -Dtests=false
	meson compile -C build/fribidi
	meson install -C build/fribidi

libass: harfbuzz fribidi
	meson setup build/libass $(TMPDIR)/libass-0.17.5 --default-library=shared \
		--cross-file "$(CURDIR)/$(OSX_ARCH).meson" --prefix=$(PREFIX)
	meson compile -C build/libass
	meson install -C build/libass

dav1d:
	meson setup build/dav1d $(TMPDIR)/dav1d-1.5.4 \
		--cross-file "$(CURDIR)/$(OSX_ARCH).meson" --prefix=$(PREFIX) \
		-Denable_tools=false -Denable_examples=false -Denable_tests=false -Denable_docs=false
	meson compile -C build/dav1d
	meson install -C build/dav1d

ffmpeg: mbedtls libass dav1d libwebp
	mkdir -p build/ffmpeg && cd build/ffmpeg && \
	$(TMPDIR)/ffmpeg-9.0.1/configure --enable-shared --disable-static --prefix=$(PREFIX) \
		--enable-pthreads --enable-runtime-cpudetect --pkg-config-flags=--static \
		--cc=clang --enable-cross-compile --target-os=darwin --arch=$(OSX_ARCH) \
		--extra-cflags="-arch $(OSX_ARCH) -mmacosx-version-min=$(OSX_TARGET)" \
		--extra-cxxflags="-arch $(OSX_ARCH) -mmacosx-version-min=$(OSX_TARGET)" \
		--extra-ldflags="-arch $(OSX_ARCH) -mmacosx-version-min=$(OSX_TARGET)" \
		--disable-autodetect --disable-encoders --enable-pic --disable-muxers \
		--disable-programs --disable-doc --disable-debug --disable-avdevice \
		--disable-libjack --disable-indev=jack --enable-network \
		--enable-libdav1d --enable-libass --enable-libfreetype --enable-zlib --enable-bzlib \
		--enable-videotoolbox --enable-audiotoolbox --enable-mbedtls --enable-version3 \
		--disable-filters --enable-filter=hflip,vflip,transpose
	$(MAKE) -C build/ffmpeg -j$(shell sysctl -n hw.ncpu)
	$(MAKE) -C build/ffmpeg install

libplacebo:
	meson setup build/libplacebo $(TMPDIR)/libplacebo \
		--cross-file "$(CURDIR)/$(OSX_ARCH).meson" --prefix=$(PREFIX) \
		-Ddemos=false -Dtests=false -Dopengl=enabled -Dlcms=disabled -Dvulkan=disabled
	meson compile -C build/libplacebo
	meson install -C build/libplacebo

mpv: uchardet ffmpeg libplacebo
	meson setup build/mpv $(TMPDIR)/mpv-0.41.0 --default-library=shared \
		--cross-file "$(CURDIR)/$(OSX_ARCH).meson" --prefix=$(PREFIX) -Dtests=false \
		-Dlibmpv=true -Dcplayer=false -Dswift-build=disabled -Dlua=disabled \
		-Dmacos-cocoa-cb=disabled -Dmacos-media-player=disabled -Dmacos-touchbar=disabled \
		-Dlibarchive=disabled -Dlcms2=disabled -Dmanpage-build=disabled -Dhtml-build=disabled
	meson compile -C build/mpv
	meson install -C build/mpv

mupdf:
	make -C $(TMPDIR)/mupdf-1.24.11-source install-libs \
		build=release shared=yes prefix=$(PREFIX) OS=MACOS \
		XCFLAGS="-arch $(OSX_ARCH) -mmacosx-version-min=$(OSX_TARGET)" \
		XLDFLAGS="-arch $(OSX_ARCH) -mmacosx-version-min=$(OSX_TARGET)" \
		XLIB_LDFLAGS="-install_name $(PREFIX)/lib/libmupdf.dylib" \
		USE_SYSTEM_ZLIB=yes USE_SYSTEM_FREETYPE=yes USE_SYSTEM_HARFBUZZ=yes \
		SYS_FREETYPE_CFLAGS="-I$(PREFIX)/include/freetype2" \
		SYS_FREETYPE_LIBS="-L$(PREFIX)/lib -lfreetype" \
		SYS_HARFBUZZ_CFLAGS="-I$(PREFIX)/include/harfbuzz" \
		-j $(shell sysctl -n hw.ncpu)