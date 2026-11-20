#!/usr/bin/env python3
"""adt-addprop.py IN.adt OUT.adt NODEPATH NAME U32 : add (or replace) a u32 property in an ADT node, e.g. /defaults kern.maxvnodes 65536."""
import struct,sys
d=bytearray(open(sys.argv[1],'rb').read()); path=[x for x in sys.argv[3].split('/') if x]; name=sys.argv[4].encode(); val=int(sys.argv[5],0)
def walk(off):
    np_,nc=struct.unpack_from('<II',d,off); p=off+8; props={}
    for _ in range(np_):
        nm=bytes(d[p:p+32]).split(b'\0')[0]; sz=struct.unpack_from('<I',d,p+32)[0]&0x7fffffff
        props[nm]=(p,sz); p+=36+((sz+3)&~3)
    kids=[]
    for _ in range(nc):
        kids.append(p); p=walk(p)[3]
    return np_,props,kids,p
def find(off,path):
    if not path: return off
    for k in walk(off)[2]:
        pr=walk(k)[1]
        if b'name' in pr:
            p,sz=pr[b'name']
            if d[p+36:p+36+sz].split(b'\0')[0].decode()==path[0]: return find(k,path[1:])
    raise SystemExit('node not found: '+path[0])
n=find(0,path); np_,props,kids,end=walk(n)
if name in props:
    p,sz=props[name]; assert sz==4; struct.pack_into('<I',d,p+36,val); print('replaced')
else:
    p=n+8
    for k in props.values(): p=max(p,k[0]+36+((k[1]+3)&~3))
    prop=name.ljust(32,b'\0')+struct.pack('<II',4,val)
    d[p:p]=prop; struct.pack_into('<I',d,n,np_+1); print('added at',p)
open(sys.argv[2],'wb').write(d)
