import re,sys
# Usage: python3 tools/cci-trace-writes.py TRACE SID  (7-bit sid as the trace prints it) -> "addr val" lines
# decode CCI writes for one sid from cam_cci_data_queue LOAD_DATA lines: -> "ts reg val" (16-bit addr, 8-bit data)
want=sys.argv[2]
cur=None; words=[]; out=[]; seqaddr=None
def flush_cmd(ws,ts):
    global seqaddr
    b=[]
    for w in ws: b+= [(w>>(8*i))&0xff for i in range(4)]
    hdr=b[0]; n=hdr>>4
    if hdr&0xf!=9: return
    cont = n==0xf
    k = 11 if cont else n
    body=b[1:1+k]
    if seqaddr is None:
        addr=(body[0]<<8)|body[1]; data=body[2:]
    else:
        addr=seqaddr; data=body
    for d in data:
        out.append((ts,addr,d)); addr+=1
    seqaddr = addr if cont else None
pend=[]; need=0; ts=''
for line in open(sys.argv[1],errors='replace'):
    line=re.sub(r'\x1b\[[0-9;]*m','',line)
    m=re.search(r'set param sid (0x[0-9a-f]+)',line)
    if m: cur=m.group(1); seqaddr=None; continue
    if cur!=want: continue
    m=re.search(r'cam_cci_data_queue: \d+: LOAD_DATA_ADDR (0x[0-9a-f]+), q: \d+, len:(\d+)',line)
    if not m: continue
    t=re.search(r'^<\d>\[\s*([\d.]+)\]',line); ts=t.group(1) if t else ts
    w=int(m.group(1),16); ln=int(m.group(2))
    pend.append(w)
    if len(pend)==ln:
        flush_cmd(pend,ts); pend=[]
for ts,a,d in out: print(ts, '0x%04x'%a, '0x%02x'%d)
