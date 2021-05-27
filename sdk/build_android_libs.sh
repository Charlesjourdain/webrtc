#!/bin/bash

set -e

export PATH=/root/src/media/webrtc_repo/webrtc_android/src/third_party/llvm-build/Release+Asserts/bin:$PATH

pushd third_party/ffmpeg

git reset --hard
git apply ../../sdk/ffmpeg-others-build.diff

python chromium/scripts/build_ffmpeg.py android arm-neon --branding Chrome -- \
    --disable-asm \
    --disable-encoders --disable-hwaccels --disable-bsfs --disable-devices --disable-filters \
    --disable-protocols --enable-protocol=file \
    --disable-parsers --enable-parser=mpegaudio --enable-parser=h264 --enable-parser=hevc \
    --disable-demuxers --enable-demuxer=mov --enable-demuxer=mp3 --enable-demuxer=mpegts \
    --disable-decoders --enable-decoder=mp3 --enable-decoder=aac \
    --disable-muxers --enable-muxer=matroska

python chromium/scripts/build_ffmpeg.py android ia32 --branding Chrome -- \
    --disable-asm \
    --disable-encoders --disable-hwaccels --disable-bsfs --disable-devices --disable-filters \
    --disable-protocols --enable-protocol=file \
    --disable-parsers --enable-parser=mpegaudio --enable-parser=h264 --enable-parser=hevc \
    --disable-demuxers --enable-demuxer=mov --enable-demuxer=mp3 --enable-demuxer=mpegts \
    --disable-decoders --enable-decoder=mp3 --enable-decoder=aac \
    --disable-muxers --enable-muxer=matroska

python chromium/scripts/build_ffmpeg.py android arm64 --branding Chrome -- \
    --disable-asm \
    --disable-encoders --disable-hwaccels --disable-bsfs --disable-devices --disable-filters \
    --disable-protocols --enable-protocol=file \
    --disable-parsers --enable-parser=mpegaudio --enable-parser=h264 --enable-parser=hevc \
    --disable-demuxers --enable-demuxer=mov --enable-demuxer=mp3 --enable-demuxer=mpegts \
    --disable-decoders --enable-decoder=mp3 --enable-decoder=aac \
    --disable-muxers --enable-muxer=matroska

python chromium/scripts/build_ffmpeg.py android x64 --branding Chrome -- \
    --disable-asm \
    --disable-encoders --disable-hwaccels --disable-bsfs --disable-devices --disable-filters \
    --disable-protocols --enable-protocol=file \
    --disable-parsers --enable-parser=mpegaudio --enable-parser=h264 --enable-parser=hevc \
    --disable-demuxers --enable-demuxer=mov --enable-demuxer=mp3 --enable-demuxer=mpegts \
    --disable-decoders --enable-decoder=mp3 --enable-decoder=aac \
    --disable-muxers --enable-muxer=matroska

./chromium/scripts/copy_config.sh
./chromium/scripts/generate_gn.py

popd

gn gen out/android_release_arm --args='target_os="android" target_cpu="arm" is_debug=false ffmpeg_branding="Chrome"'
ninja -C out/android_release_arm libjingle_peerconnection_so
cp out/android_release_arm/libjingle_peerconnection_so.so ../../webrtc_ios/src/sdk/android_gradle/webrtc/prebuilt_libs/armeabi-v7a/

gn gen out/android_release_x86 --args='target_os="android" target_cpu="x86" is_debug=false ffmpeg_branding="Chrome"'
ninja -C out/android_release_x86 libjingle_peerconnection_so
cp out/android_release_x86/libjingle_peerconnection_so.so ../../webrtc_ios/src/sdk/android_gradle/webrtc/prebuilt_libs/x86/

gn gen out/android_release_arm64 --args='target_os="android" target_cpu="arm64" is_debug=false ffmpeg_branding="Chrome"'
ninja -C out/android_release_arm64 libjingle_peerconnection_so
cp out/android_release_arm64/libjingle_peerconnection_so.so ../../webrtc_ios/src/sdk/android_gradle/webrtc/prebuilt_libs/arm64-v8a/

gn gen out/android_release_x64 --args='target_os="android" target_cpu="x64" is_debug=false ffmpeg_branding="Chrome"'
ninja -C out/android_release_x64 libjingle_peerconnection_so
cp out/android_release_x64/libjingle_peerconnection_so.so ../../webrtc_ios/src/sdk/android_gradle/webrtc/prebuilt_libs/x86_64/
