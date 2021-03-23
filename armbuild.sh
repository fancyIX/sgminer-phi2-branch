# Compiles libconfig for Android
#NDK 下载地址：https://developer.android.google.cn/ndk/downloads?hl=zh-cn 版本：android-ndk-r19c-linux-x86_64.zip
 
export NDK=/home/mmdev/Android/Sdk/ndk/20.1.5948944
export INSTALL_DIR="`pwd`/jni_arm"
export API=28
export TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64
export TARGET=aarch64-linux-android
export SYS_ROOT=$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot
export CC=$TOOLCHAIN/bin/$TARGET$API-clang
export AS=$CC
export LD=$TOOLCHAIN/bin/ld
export AR=$TOOLCHAIN/bin/llvm-ar
export RANLIB=$TOOLCHAIN/bin/aarch64-linux-android-ranlib
export STRIP=$TOOLCHAIN/bin/llvm-strip
export CXX=$TOOLCHAIN/bin/$TARGET$API-clang++

export 





export CFLAGS="--sysroot=${SYS_ROOT} -I$SYS_ROOT/usr/include   -march=armv8-a+fp+simd+crypto+crc -mfpu=neon" 
export C_INCLUDE_PATH="$NDK/sysroot/usr/include:$NDK/sysroot/usr/include/aarch64-linux-android"
export LDFLAGS="-Wl,-L${SYS_ROOT}/usr/lib -L${TOOLCHAIN}/lib"
export CMAKE="/home/mmdev/Android/Sdk/ndk/20.1.5948944/prebuilt/linux-x86_64/bin/make"
#autoreconf -i

 
mkdir -p $INSTALL_DIR
./configure  --host=$TARGET --disable-adl  --disable-git-version
            	
make
