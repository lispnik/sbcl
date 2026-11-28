#!/bin/sh
# Build Mbed TLS for the DOS port of SBCL, so that sockets can speak TLS.
#
# Mbed TLS (https://github.com/Mbed-TLS/mbedtls, the 3.6 series) is a TLS
# library in portable C.  It is built here with Open Watcom, as Watt-32 is
# (tools-for-build/dos-watt32.sh), as a TLS 1.2 and 1.3 client and nothing
# more.  The runtime reaches it through src/runtime/dostls.c, which also
# gives it what DOS lacks: random numbers, a clock and a calendar.
# The Mbed TLS tree is not modified: a copy is configured and built in
# $SBCL_DOS_TOOLS/mbedtls-build.
#
# usage: dos-mbedtls.sh [SOURCE]
#   (default: the mbedtls-3.6.* directory in $SBCL_DOS_TOOLS/dl-mbedtls)
# result: $SBCL_DOS_TOOLS/mbedtls-build/mbedtls.lib, which
# src/runtime/Makefile.dos links in if it is there.  Needs Python 3.
set -e
here=`cd "\`dirname "$0"\`" && pwd`
: ${SBCL_DOS_TOOLS:=`cd "$here/../.." && pwd`/sbcl-dos-tools}
src=${1:-`ls -d $SBCL_DOS_TOOLS/dl-mbedtls/mbedtls-3.6.*/ | tail -1 | sed "s,/$,,"`}
[ -f "$src/include/mbedtls/mbedtls_config.h" ] || { echo "no Mbed TLS source in $src"; exit 1; }
B=$SBCL_DOS_TOOLS/mbedtls-build
export WATCOM=$SBCL_DOS_TOOLS/ow
case "`uname -s`-`uname -m`" in
  Darwin-arm64) owbin=armo64 ;; Darwin-x86_64) owbin=bino64 ;;
  Linux-aarch64) owbin=arml64 ;; *) owbin=binl64 ;;
esac
PATH=$WATCOM/$owbin:$PATH; export PATH
export INCLUDE=$WATCOM/h
# As Watt-32: Watcom's own stack-based convention.  -za99 for the C99 that
# Mbed TLS is written in.
CFLAGS="-zq -bt=dos -mf -6s -fpi87 -fp6 -s -oilrtm -zlf -za99 -wcd=102 -wcd=111 -wcd=124 -wcd=201"

rm -rf $B/src $B/obj
mkdir -p $B/obj
cp -R "$src" $B/src
cd $B/src

# 1. A client over a socket of ours: no servers, no datagrams, no sockets
#    or timers of Mbed TLS's own, nothing kept on disk, no assembly.
for s in MBEDTLS_HAVE_ASM MBEDTLS_NET_C MBEDTLS_TIMING_C MBEDTLS_AESNI_C MBEDTLS_PADLOCK_C \
    MBEDTLS_PSA_CRYPTO_STORAGE_C MBEDTLS_PSA_ITS_FILE_C MBEDTLS_PSA_CRYPTO_SE_C \
    MBEDTLS_SSL_PROTO_DTLS MBEDTLS_SSL_DTLS_ANTI_REPLAY MBEDTLS_SSL_DTLS_HELLO_VERIFY \
    MBEDTLS_SSL_DTLS_CLIENT_PORT_REUSE MBEDTLS_SSL_DTLS_CONNECTION_ID \
    MBEDTLS_SSL_COOKIE_C MBEDTLS_SSL_SRV_C MBEDTLS_SSL_CACHE_C MBEDTLS_SSL_TICKET_C \
    MBEDTLS_SSL_CONTEXT_SERIALIZATION MBEDTLS_LMS_C MBEDTLS_SELF_TEST MBEDTLS_DEBUG_C \
    MBEDTLS_VERSION_FEATURES MBEDTLS_X509_CSR_PARSE_C MBEDTLS_X509_CSR_WRITE_C \
    MBEDTLS_X509_CRT_WRITE_C MBEDTLS_X509_CREATE_C MBEDTLS_PKCS7_C MBEDTLS_PKCS12_C
do python3 scripts/config.py unset $s; done
#    What DOS (or Watcom's C library: memset_s) lacks comes from dostls.c;
#    and with one thread, nobody changes a buffer while the library reads it.
for s in MBEDTLS_NO_PLATFORM_ENTROPY MBEDTLS_ENTROPY_HARDWARE_ALT \
    MBEDTLS_PLATFORM_MS_TIME_ALT MBEDTLS_PLATFORM_GMTIME_R_ALT \
    MBEDTLS_PSA_ASSUME_EXCLUSIVE_BUFFERS MBEDTLS_PLATFORM_ZEROIZE_ALT
do python3 scripts/config.py set $s; done

# 2. Three places use C99 that Open Watcom does not have: aggregates
#    initialized with values known only at run time, and compound literals.
#    They are rewritten in the copy.
change() {      # change FILE SED-EXPRESSION: the expression must change the file
    cp $1 $1.orig
    sed -i.bak "$2" $1
    cmp -s $1 $1.orig && { echo "the change to $1 no longer applies"; exit 1; }
    rm -f $1.orig $1.bak
}
change library/asn1parse.c \
    's/^    asn1_get_sequence_of_cb_ctx_t cb_ctx = { tag, cur };$/    asn1_get_sequence_of_cb_ctx_t cb_ctx; cb_ctx.tag = tag; cb_ctx.cur = cur;/'
change library/bignum.c \
    's/^    void \* const p\[2\] = { X->p, Y->p };$/    void *p[2]; p[0] = X->p; p[1] = Y->p;/'
change library/psa_crypto.c \
    's/^    \*local_input = PSA_CRYPTO_LOCAL_INPUT_INIT;$/    memset(local_input, 0, sizeof(*local_input));/'
change library/psa_crypto.c \
    's/^    \*local_output = PSA_CRYPTO_LOCAL_OUTPUT_INIT;$/    memset(local_output, 0, sizeof(*local_output));/'

# 3. Compile
echo "//compiling Mbed TLS"
fail=
for c in library/*.c; do
    o=$B/obj/`basename $c .c`.obj
    wcc386 $CFLAGS -i=include -i=library -fo=$o $c > $B/obj/`basename $c .c`.err 2>&1 || fail="$fail $c"
done
[ -z "$fail" ] || { echo "failed:$fail (see $B/obj/*.err)"; exit 1; }
rm -f $B/mbedtls.lib
wlib -q -b -c $B/mbedtls.lib `ls $B/obj/*.obj | sed 's/^/+/'`
ls -la $B/mbedtls.lib
