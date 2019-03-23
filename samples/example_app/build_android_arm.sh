#/bin/sh
#script to compile Wrapper for Android

export ANDROIDTC="$HOME/android-tc"
export ANDROIDTC=$HOME/Android/Toolchains/arm
export ANDROID_TC=$HOME/Android/Toolchains/arm
export TCBINARIES="$ANDROIDTC/bin"
export TCINCLUDES="$ANDROIDTC/arm-linux-androideabi"
export TCSYSROOT="$ANDROIDTC/sysroot"
export STDCPPTC="$TCINCLUDES/lib/armv7-a/libstdc++.a"
export BOINC_DIR="../.."
export BOINC_API_DIR="$BOINC_DIR/api"
export BOINC_LIB_DIR="$BOINC_DIR/lib"
export BOINC_ZIP_DIR="$BOINC_DIR/zip"

export PATH="$TCBINARIES:$TCINCLUDES/bin:$PATH"
export CC=arm-linux-androideabi-clang
export CXX=arm-linux-androideabi-clang++
export LD=arm-linux-androideabi-ld
export CFLAGS="--sysroot=$TCSYSROOT -DANDROID -DDECLARE_TIMEZONE -Wall -I$TCINCLUDES/include -O3 -fomit-frame-pointer -I$TCINCLUDES/include -I$BOINC_DIR -I$BOINC_LIB_DIR -I$BOINC_API_DIR -I$BOINC_ZIP_DIR"
export CXXFLAGS="--sysroot=$TCSYSROOT -DANDROID -Wall  -funroll-loops -fexceptions -O3 -fomit-frame-pointer -I$TCINCLUDES/include -I$BOINC_DIR -I$BOINC_LIB_DIR -I$BOINC_API_DIR -I$BOINC_ZIP_DIR"
export LDFLAGS="-L$TCSYSROOT/usr/lib -L$TCINCLUDES/lib -L$BOINC_DIR -L$BOINC_LIB_DIR -L$BOINC_API_DIR -L$BOINC_ZIP_DIR -llog -march=native -latomic -static-libstdc++ -fPIE -pie"
export GDB_CFLAGS="--sysroot=$TCSYSROOT -Wall -g -I$TCINCLUDES/include"

# Prepare android toolchain and environment
../../android/build_androidtc_arm.sh

make -f Makefile_android
