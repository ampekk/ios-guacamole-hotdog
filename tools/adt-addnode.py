#!/usr/bin/env python3
"""adt-addnode.py IN.adt OUT.adt NAME PROP=u64:VALUE|u32:VALUE|str:TEXT ... : append a child node NAME to the ADT root
(e.g. machine-timeouts debug-ack=u64:19200000). Fails if the root already has a child of that name."""
import struct, sys
d = bytearray(open(sys.argv[1], 'rb').read()); name = sys.argv[3]
def end_of(off):
    np_, nc = struct.unpack_from('<II', d, off); p = off + 8; props = {}
    for _ in range(np_):
        nm = bytes(d[p:p + 32]).split(b'\0')[0]; sz = struct.unpack_from('<I', d, p + 32)[0] & 0x7fffffff
        props[nm] = d[p + 36:p + 36 + sz]; p += 36 + ((sz + 3) & ~3)
    kids = []
    for _ in range(nc):
        kids.append(p); p = end_of(p)[0]
    return p, props, kids
end, _, kids = end_of(0)
for k in kids:
    if bytes(end_of(k)[1].get(b'name', b'')).split(b'\0')[0] == name.encode():
        sys.exit('root already has ' + name)
def prop(n, v):
    return n.encode().ljust(32, b'\0') + struct.pack('<I', len(v)) + v + b'\0' * ((-len(v)) % 4)
props = [prop('name', name.encode() + b'\0')]
for a in sys.argv[4:]:
    k, v = a.split('=', 1); t, x = v.split(':', 1)
    val = {'u64': lambda: struct.pack('<Q', int(x, 0)), 'u32': lambda: struct.pack('<I', int(x, 0)),
           'str': lambda: x.encode() + b'\0'}[t]()
    props.append(prop(k, val))
node = struct.pack('<II', len(props), 0) + b''.join(props)
assert end == len(d), 'root subtree does not end at the end of the file'
d += node
struct.pack_into('<I', d, 4, struct.unpack_from('<I', d, 4)[0] + 1)
open(sys.argv[2], 'wb').write(d); print('added', name, len(node), 'bytes')
