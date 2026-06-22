TOOLCHAIN := $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64
ABI ?= arm64-v8a
PREFIX ?= $(PWD)/jniLibs/$(ABI)
TMPDIR ?= /tmp
apilvl=21

export LDFLAGS=-Wl,-z,max-page-size=16384
export PATH=$(TOOLCHAIN)/bin:$(PREFIX)/bin:$(shell printenv PATH)
export PKG_CONFIG_SYSROOT_DIR=$(PREFIX)
export PKG_CONFIG_LIBDIR=$(PREFIX)/lib/pkgconfig

ifeq ($(ABI),armeabi-v7a)
	CPU=armv7-a
	CC=armv7a-linux-androideabi$(apilvl)-clang
	CPUFLAGS=-mfpu=neon -mcpu=cortex-a8
else ifeq ($(ABI),arm64-v8a)
	CPU=armv8-a
	CC=aarch64-linux-android$(apilvl)-clang
else ifeq ($(ABI),x86_64)
	CPU=generic
	CC=x86_64-linux-android$(apilvl)-clang
endif

all: download build
	@echo Using toolchain ${TOOLCHAIN}

build: prefix mpv curl clean

clean:
	@rm -rf build

prefix:
	mkdir -p $(PREFIX)
	ln -s . $(PREFIX)/usr
	ln -s . $(PREFIX)/local

download:
	wget -qO- https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz | tar zxf - -C $(TMPDIR)
	wget -qO- https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.6/mbedtls-3.6.6.tar.bz2 | tar jxf - -C $(TMPDIR)
	wget -qO- https://curl.se/download/curl-8.16.0.tar.xz | tar Jxf - -C $(TMPDIR)
	wget -qO- https://github.com/webmproject/libwebp/archive/v1.6.0.tar.gz | tar zxf - -C $(TMPDIR)
	wget -qO- https://downloads.sourceforge.net/project/freetype/freetype2/2.14.3/freetype-2.14.3.tar.xz | tar Jxf - -C $(TMPDIR)
	wget -qO- https://github.com/harfbuzz/harfbuzz/releases/download/14.2.1/harfbuzz-14.2.1.tar.xz | tar Jxf - -C $(TMPDIR)
	wget -qO- https://github.com/fribidi/fribidi/releases/download/v1.0.16/fribidi-1.0.16.tar.xz | tar Jxf - -C $(TMPDIR)
	wget -qO- https://github.com/libass/libass/releases/download/0.17.5/libass-0.17.5.tar.xz | tar Jxf - -C $(TMPDIR)
	wget -qO- https://downloads.videolan.org/pub/videolan/dav1d/1.5.3/dav1d-1.5.3.tar.xz | tar Jxf - -C $(TMPDIR)
	wget -qO- https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz | tar Jxf - -C $(TMPDIR)
	wget -qO- https://github.com/mpv-player/mpv/archive/v0.41.0.tar.gz | tar zxf - -C $(TMPDIR)
	git clone https://github.com/haasn/libplacebo.git -b v7.360.1 --depth 1 --recurse-submodules $(TMPDIR)/libplacebo
	wget -qO- https://github.com/Mbed-TLS/mbedtls/pull/10674.patch | patch -d $(TMPDIR)/mbedtls-3.6.6 -Nbp1
	patch -d $(TMPDIR)/libwebp-1.6.0 -Nbp1 -i $(PWD)/scripts/mingw64/libwebp/0001-mingw-cmake-output.patch

SDL2:
	cmake -B build/SDL2 -S $(TMPDIR)/SDL2-2.32.10 -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE="$(ANDROID_NDK)/build/cmake/android.toolchain.cmake" \
		-DANDROID_ABI=$(ABI) -DANDROID_PLATFORM=android-$(apilvl) -DCMAKE_WARN_DEPRECATED=FALSE \
		-DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
	cmake --build build/SDL2
	DESTDIR=$(PREFIX) cmake --install build/SDL2

mbedtls:
	cmake -B build/mbedtls -S $(TMPDIR)/mbedtls-3.6.6 -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE="$(ANDROID_NDK)/build/cmake/android.toolchain.cmake" \
		-DANDROID_ABI=$(ABI) -DANDROID_PLATFORM=android-$(apilvl) -DCMAKE_WARN_DEPRECATED=FALSE \
		-DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON \
		-DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF
	cmake --build build/mbedtls
	DESTDIR=$(PREFIX) cmake --install build/mbedtls

curl:
	cmake -B build/curl -S $(TMPDIR)/curl-8.16.0 -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE="$(ANDROID_NDK)/build/cmake/android.toolchain.cmake" \
		-DANDROID_ABI=$(ABI) -DANDROID_PLATFORM=android-$(apilvl) -DCMAKE_WARN_DEPRECATED=FALSE \
		-DBUILD_SHARED_LIBS=ON -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF \
		-DBUILD_LIBCURL_DOCS=OFF -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF \
		-DUSE_LIBIDN2=OFF -DUSE_NGHTTP2=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF \
		-DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBPSL=OFF -DCURL_USE_MBEDTLS=ON -DCURL_USE_PKGCONFIG=ON \
		-DHTTP_ONLY=ON -DCURL_DISABLE_PROGRESS_METER=ON -DBUILD_SHARED_LIBS=ON
	cmake --build build/curl
	DESTDIR=$(PREFIX) cmake --install build/curl

libwebp:
	cmake -B build/libwebp -S $(TMPDIR)/libwebp-1.6.0 -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE="$(ANDROID_NDK)/build/cmake/android.toolchain.cmake" \
		-DANDROID_ABI=$(ABI) -DANDROID_PLATFORM=android-$(apilvl) -DCMAKE_WARN_DEPRECATED=FALSE \
		-DWEBP_BUILD_EXTRAS=OFF -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF \
		-DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF \
		-DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_LIBWEBPMUX=OFF -DBUILD_SHARED_LIBS=OFF
	cmake --build build/libwebp
	DESTDIR=$(PREFIX) cmake --install build/libwebp

freetype:
	meson setup build/freetype $(TMPDIR)/freetype-2.14.3 --cross-file "$(CURDIR)/$(ABI).meson" \
		-Dharfbuzz=disabled -Dbrotli=disabled -Dzlib=enabled
	meson compile -C build/freetype
	meson install -C build/freetype --destdir $(PREFIX)

harfbuzz: freetype
	meson setup build/harfbuzz $(TMPDIR)/harfbuzz-14.2.1 --cross-file "$(CURDIR)/$(ABI).meson" \
		-Dfreetype=enabled -Dglib=disabled -Dgobject=disabled -Dcairo=disabled \
		-Dchafa=disabled -Dtests=disabled -Dintrospection=disabled \
		-Ddocs=disabled -Ddoc_tests=false -Dutilities=disabled
	meson compile -C build/harfbuzz
	meson install -C build/harfbuzz --destdir $(PREFIX)

fribidi:
	meson setup build/fribidi $(TMPDIR)/fribidi-1.0.16 --cross-file "$(CURDIR)/$(ABI).meson" \
		-Dbin=false -Ddocs=false -Dtests=false
	meson compile -C build/fribidi
	meson install -C build/fribidi --destdir $(PREFIX)

libass: harfbuzz fribidi
	meson setup build/libass $(TMPDIR)/libass-0.17.5 --cross-file "$(CURDIR)/$(ABI).meson" \
		-Drequire-system-font-provider=false
	meson compile -C build/libass
	meson install -C build/libass --destdir $(PREFIX)

dav1d:
	meson setup build/dav1d $(TMPDIR)/dav1d-1.5.3 --cross-file "$(CURDIR)/$(ABI).meson" \
		-Db_lto=true -Dstack_alignment=16 -Denable_tools=false \
		-Denable_examples=false -Denable_tests=false -Denable_docs=false
	meson compile -C build/dav1d
	meson install -C build/dav1d --destdir $(PREFIX)

ffmpeg: mbedtls libass dav1d libwebp
	mkdir -p build/ffmpeg && cd build/ffmpeg && \
	$(TMPDIR)/ffmpeg-8.1.2/configure --enable-shared --disable-static --pkg-config=pkg-config \
		--target-os=android --arch=$(firstword $(subst -, ,$(CC))) --cpu=$(CPU) --cc=$(CC) \
		--enable-cross-compile --cross-prefix=llvm- --sysroot=$(TOOLCHAIN)/sysroot \
		--extra-cflags="-I$(PREFIX)/include $(CPUFLAGS)" --extra-ldflags="-L$(PREFIX)/lib" \
		--disable-programs --disable-debug --disable-doc --enable-network \
		--disable-vulkan --disable-devices --disable-autodetect --enable-jni \
		--enable-mediacodec --enable-libass --enable-libdav1d --enable-zlib \
		--disable-muxers --disable-encoders --enable-mbedtls --enable-version3 \
		--disable-filters --enable-filter=hflip,vflip,transpose
	make -C build/ffmpeg -j$(shell nproc)
	make -C build/ffmpeg install DESTDIR=$(PREFIX)

libplacebo:
	meson setup build/libplacebo $(TMPDIR)/libplacebo \
		--cross-file "$(CURDIR)/$(ABI).meson" \
		-Ddemos=false -Dtests=false -Dlcms=disabled -Dvulkan=disabled
	meson compile -C build/libplacebo
	meson install -C build/libplacebo --destdir $(PREFIX)

mpv: SDL2 ffmpeg libplacebo
	meson setup build/mpv $(TMPDIR)/mpv-0.41.0 \
		--cross-file "$(CURDIR)/$(ABI).meson" --default-library=shared \
		-Dlibmpv=true -Dcplayer=false -Dtests=false -Diconv=disabled \
		-Dsdl2-audio=enabled -Dsdl2-video=enabled -Dlua=disabled \
		-Dmanpage-build=disabled -Dlibarchive=disabled 
	meson compile -C build/mpv
	meson install -C build/mpv --destdir $(PREFIX)