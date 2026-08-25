#!/usr/bin/env bash
set -euo pipefail

: "${VITASDK:?Set VITASDK to the VitaSDK installation directory}"
: "${JOBS:=12}"

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/.." && pwd)
ffmpeg_commit=239f2c733de417201d7ad3b3b8b0d9b63285b2b1
source_dir=${KRKRVITA_FFMPEG_SOURCE_DIR:-$repo_root/.cache/src/ffmpeg-8.1.1}
prefix=${KRKRVITA_FFMPEG_PREFIX:-$repo_root/.cache/ffmpeg-vita-8.1.1}
build_signature="ffmpeg-$ffmpeg_commit-vita-release-no-debug-v1"
build_stamp=$prefix/.krkrvita-build

if [[ ! -x $VITASDK/bin/arm-vita-eabi-gcc ]]; then
    echo "Vita compiler not found under VITASDK: $VITASDK" >&2
    exit 1
fi
if [[ ! -f $VITASDK/arm-vita-eabi/lib/libmp3lame.a ]]; then
    echo "VitaSDK libmp3lame.a is required by the existing Yuri audio backend" >&2
    exit 1
fi
if [[ ! $JOBS =~ ^[1-9][0-9]*$ ]]; then
    echo "JOBS must be a positive integer" >&2
    exit 2
fi

archives_ready=true
for archive in avformat avcodec avutil swresample swscale; do
    if [[ ! -s $prefix/lib/lib${archive}.a ]]; then
        archives_ready=false
    fi
done
if $archives_ready && [[ -f $build_stamp ]] &&
   grep -Fqx -- "$build_signature" "$build_stamp"; then
    echo "Pinned release FFmpeg is already installed at $prefix"
    exit 0
fi

if [[ ! -d $source_dir/.git ]]; then
    mkdir -p -- "$(dirname -- "$source_dir")"
    git clone --filter=blob:none https://github.com/FFmpeg/FFmpeg.git "$source_dir"
fi

git -C "$source_dir" fetch --depth=1 origin "$ffmpeg_commit"
git -C "$source_dir" checkout --detach "$ffmpeg_commit"
if [[ $(git -C "$source_dir" rev-parse HEAD) != "$ffmpeg_commit" ]]; then
    echo "FFmpeg source did not resolve to the pinned revision" >&2
    exit 1
fi

mkdir -p -- "$prefix"
cd -- "$source_dir"
if [[ -f ffbuild/config.mak ]]; then
    make distclean
fi
./configure \
    --prefix="$prefix" \
    --enable-cross-compile \
    --cross-prefix="$VITASDK/bin/arm-vita-eabi-" \
    --arch=armv7-a \
    --cpu=cortex-a9 \
    --target-os=none \
    --disable-shared \
    --enable-static \
    --disable-programs \
    --disable-debug \
    --disable-doc \
    --disable-network \
    --disable-everything \
    --disable-runtime-cpudetect \
    --disable-armv5te \
    --disable-armv6t2 \
    --disable-bzlib \
    --disable-iconv \
    --disable-lzma \
    --disable-sdl2 \
    --disable-securetransport \
    --disable-xlib \
    --enable-avcodec \
    --enable-avformat \
    --enable-avutil \
    --enable-swresample \
    --enable-swscale \
    --enable-libmp3lame \
    --enable-decoder=aac,adpcm_ima_qt,alac,ape,ac3,cinepak,flac,h264,mp2,mp3,mpeg1video,mpeg2video,mpeg4,opus,pcm_s16le,pcm_s24le,pcm_s32le,pcm_s8,pcm_u8,svq1,tta,vc1,vorbis,wmalossless,wavpack,wma2,wmav2,wmv3 \
    --enable-demuxer=aac,ac3,aiff,ape,asf,avi,caf,cine,flac,matroska,mp3,mpegps,mov,mp4,m4a,pcm_s16le,pcm_s24le,pcm_s32le,pcm_s8,pcm_u8,ogg,opus,tta,wav,webm,wv \
    --enable-muxer=aac,ac3,aiff,ape,asf,avi,caf,flac,matroska,mp3,mp4,m4a,mov,pcm_s16le,pcm_s24le,pcm_s32le,pcm_s8,pcm_u8,ogg,opus,tta,wav,webm,wv \
    --enable-encoder=aac,alac,ape,ac3,flac,libmp3lame,mpeg4,opus,pcm_s16le,pcm_s24le,pcm_s32le,pcm_s8,pcm_u8,tta,vorbis,wmalossless,wavpack \
    --enable-parser=aac,ac3,flac,h263,h264,mpeg4video,mpegaudio,mpegvideo,opus,vc1,vorbis \
    --enable-protocol=file \
    --enable-pthreads \
    --extra-cflags="-std=gnu11 -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -Wl,-q -O2 -ftree-vectorize -fomit-frame-pointer -ffast-math -D_BSD_SOURCE -I$VITASDK/arm-vita-eabi/include" \
    --extra-cxxflags="-Wl,-q -O2 -ftree-vectorize -fomit-frame-pointer -ffast-math -fno-rtti -fno-exceptions -std=gnu++11 -Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types -D_BSD_SOURCE -I$VITASDK/arm-vita-eabi/include" \
    --extra-ldflags="-L$VITASDK/arm-vita-eabi/lib"

make -j"$JOBS"
make install

for archive in avformat avcodec avutil swresample swscale; do
    test -s "$prefix/lib/lib${archive}.a"
done

printf '%s\n' "$build_signature" > "$build_stamp"

echo "Installed pinned Vita FFmpeg $ffmpeg_commit to $prefix"
