#!/usr/bin/env python3
"""tracev3-unlzbm.py SRC_DIR DST_DIR : rewrite iOS 27 unified-log files so that research/ios26/tools/tracev3.py can
read them. 27.2's logd compresses each chunkset (tag 0x600d) with LZBITMAP ("ZBM" magic) instead of LZ4 ("bv41");
only Apple's libcompression decodes it, so this runs ON THE MAC (python3 + /usr/lib/libcompression.dylib).
Every *.tracev3 under SRC_DIR is copied to the same relative path under DST_DIR with its LZBITMAP chunksets
replaced by uncompressed "bv4-" blocks; other chunks are copied as they are. Prints per-file counts."""
import ctypes, os, struct, sys

COMPRESSION_LZBITMAP = 0x702
lib = ctypes.CDLL('/usr/lib/libcompression.dylib')
lib.compression_decode_buffer.restype = ctypes.c_size_t
lib.compression_decode_buffer.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t,
                                          ctypes.c_void_p, ctypes.c_int]
CAP = 64 << 20
out = ctypes.create_string_buffer(CAP)


def unz(body):
    n = lib.compression_decode_buffer(out, CAP, body, len(body), None, COMPRESSION_LZBITMAP)
    return out.raw[:n] if n else None


def convert(src, dst):
    d = open(src, 'rb').read()
    res = bytearray(); i = 0; done = fail = other = 0
    while i + 16 <= len(d):
        tag, sub, sz = struct.unpack_from('<IIQ', d, i)
        if sz > len(d) - i - 16:
            break
        body = d[i + 16:i + 16 + sz]
        if tag == 0x600d and body[:3] == b'ZBM':
            raw = unz(body)
            if raw:
                body = b'bv4-' + struct.pack('<I', len(raw)) + raw + b'bv4$'
                done += 1
            else:
                fail += 1
        else:
            other += 1
        res += struct.pack('<IIQ', tag, sub, len(body)) + body
        res += b'\0' * ((-len(res)) & 7)
        i += 16 + sz; i = (i + 7) & ~7
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, 'wb').write(res)
    print(f'{src}: {done} chunksets decoded, {fail} failed, {other} other chunks')


src, dst = sys.argv[1], sys.argv[2]
for root, _, files in os.walk(src):
    for f in files:
        if f.endswith('.tracev3'):
            p = os.path.join(root, f)
            convert(p, os.path.join(dst, os.path.relpath(p, src)))
