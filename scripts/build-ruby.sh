#!/usr/bin/env bash
# Cross-compiles CRuby 3.1 (static, no shared libraries, no dlopen) for the PS5. Run inside Ubuntu
# (WSL) as the build user. This is the first checkpoint for running RPG Maker XP/VX/Ace games with
# mkxp-z, which embeds the real Ruby interpreter.
#
# Needs a native Ruby of the same version as a helper ("baseruby"):
#   mkdir ruby-native-build && cd ruby-native-build
#   CC="gcc -std=gnu17" ../ruby-3.1.7/configure --prefix=$HOME/easyrpg/ruby-native \
#       --disable-install-doc --disable-shared && make -j && make install
# (-std=gnu17: Ruby 3.1 does not build with the C23 default of newer GCC versions.)
#
# Usage: build-ruby.sh [ruby-source-dir] [native-ruby-prefix] [build-dir]
set -eu

export PS5_PAYLOAD_SDK="${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}"
SRC="${1:-$HOME/easyrpg/ruby-3.1.7}"
NATIVE="${2:-$HOME/easyrpg/ruby-native}"
BUILD="${3:-$HOME/easyrpg/ruby-ps5-build}"

# The SDK's clang wrapper does not search the homebrew prefix that the libraries install into.
export CPATH="$PS5_PAYLOAD_SDK/target/user/homebrew/include"
export LIBRARY_PATH="$PS5_PAYLOAD_SDK/target/user/homebrew/lib"

source "$PS5_PAYLOAD_SDK/toolchain/prospero.sh"

# Extensions that get linked into the interpreter statically (there is no dlopen for .so files).
EXTS="zlib,stringio,strscan,etc,date,json,monitor,digest,digest/md5,digest/sha1,pathname,cgi/escape,erb/escape,rbconfig/sizeof,fcntl"

rm -rf "$BUILD"
mkdir -p "$BUILD/stubs"
cd "$BUILD"

# The PS5 libc has no separate libm (the math functions are in libc), but configure links with -lm.
# Provide empty stand-ins for the libraries Ruby asks for, kept out of the SDK.
for lib in m; do
    "$PS5_PAYLOAD_SDK/bin/prospero-ar" rc "$BUILD/stubs/lib$lib.a"
done

# Ruby falls back to its bundled crypt() when the system has none, and that copy's setkey/encrypt
# clash with the declarations in the PS5 libc headers. String#crypt is irrelevant for games, so give
# it a stand-in crypt header and library instead.
mkdir -p "$BUILD/stubs/include"
cat > "$BUILD/stubs/include/crypt.h" <<'EOT'
char *crypt(const char *key, const char *setting);
EOT
cat > "$BUILD/stubs/crypt_stub.c" <<'EOT'
char *crypt(const char *key, const char *setting) { (void)key; (void)setting; return 0; }
EOT
"$CC" -c -O2 "$BUILD/stubs/crypt_stub.c" -o "$BUILD/stubs/crypt_stub.o"
"$PS5_PAYLOAD_SDK/bin/prospero-ar" rc "$BUILD/stubs/libcrypt.a" "$BUILD/stubs/crypt_stub.o"
export CPATH="$CPATH:$BUILD/stubs/include"

# A few POSIX functions that Ruby references but the PS5 libc does not provide (there is no exec or
# password database in the sandbox anyway). Anything that links libruby-static.a needs -lcompat.
cat > "$BUILD/stubs/compat.c" <<'EOT'
void endpwent(void) {}
int execl(const char *path, const char *arg, ...) { (void)path; (void)arg; return -1; }
int execle(const char *path, const char *arg, ...) { (void)path; (void)arg; return -1; }
EOT
"$CC" -c -O2 "$BUILD/stubs/compat.c" -o "$BUILD/stubs/compat.o"
"$PS5_PAYLOAD_SDK/bin/prospero-ar" rc "$BUILD/stubs/libcompat.a" "$BUILD/stubs/compat.o"

"$SRC/configure" \
    --prefix=/user/homebrew \
    --host=x86_64-pc-freebsd --build=x86_64-linux-gnu \
    --with-baseruby="$NATIVE/bin/ruby" \
    --disable-shared --enable-static --with-static-linked-ext \
    --disable-install-doc --disable-rpath --disable-dtrace --disable-jit-support \
    --without-gmp --without-readline --without-baseruby-warning \
    --with-ext="$EXTS" \
    CFLAGS="-O2" LDFLAGS="-L$BUILD/stubs" LIBS="-lcompat" 2>&1 | tail -40

make -j"$(nproc)"

# Install into the SDK (headers, ruby-3.1.pc, the standard library's .rb files, ...). The prefix
# above is the path on the console, so stage the files under the SDK's sysroot with DESTDIR.
HB="$PS5_PAYLOAD_SDK/target/user/homebrew"
# (PKG_CONFIG= skips a self-check of the generated ruby-3.1.pc that the SDK's pkg-config wrapper fails.)
make install PKG_CONFIG= DESTDIR="$PS5_PAYLOAD_SDK/target"

# Ruby's own libruby-static.a does not contain the statically linked extensions (zlib, stringio,
# ...) or the encodings, and carries dummy versions of their init functions. Merge everything into
# one archive so that linking against it (like mkxp-z does) just works.
MRI_SCRIPT="$BUILD/merge.mri"
{
    echo "CREATE $BUILD/libruby-static-full.a"
    echo "ADDLIB $HB/lib/libruby-static.a"
    echo "ADDLIB $BUILD/enc/libenc.a"
    echo "ADDLIB $BUILD/enc/libtrans.a"
    for a in "$BUILD"/ext/*/*.a "$BUILD"/ext/*/*/*.a; do echo "ADDLIB $a"; done
    echo "SAVE"
    echo "END"
} > "$MRI_SCRIPT"
"$PS5_PAYLOAD_SDK/bin/prospero-ar" -M < "$MRI_SCRIPT"
"$PS5_PAYLOAD_SDK/bin/prospero-ar" d "$BUILD/libruby-static-full.a" dmyext.o dmyenc.o
"$PS5_PAYLOAD_SDK/bin/prospero-ar" q "$BUILD/libruby-static-full.a" "$BUILD/ext/extinit.o" "$BUILD/enc/encinit.o"
cp "$BUILD/libruby-static-full.a" "$HB/lib/libruby-static.a"

# The generated ruby-3.1.pc asks for a shared libruby (which does not exist here) and carries linker
# flags of this build directory; point it at the static library and the libraries it needs.
sed -i 's/^Libs:.*/Libs: -lruby-static ${LIBS} -lz -lpthread -lcrypt/' "$HB/lib/pkgconfig/ruby-3.1.pc"

# Consumers also need the stand-in libraries (see above) when they link libruby-static.a.
cp "$BUILD/stubs/libcrypt.a" "$BUILD/stubs/libcompat.a" "$HB/lib/"
echo "installed Ruby into $HB"
