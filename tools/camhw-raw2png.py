import sys, numpy as np
from PIL import Image
d=np.fromfile(sys.argv[1],dtype=np.uint8)
R=len(d)//5008; raw=d[:R*5008].reshape(R,5008)[:,:5000]
g=raw.reshape(R,1000,5).astype(np.uint16)
px=np.empty((R,4000),np.uint16)
for k in range(4): px[:,k::4]=(g[:,:,k]<<2)|((g[:,:,4]>>(2*k))&3)
print(R, np.percentile(px,[1,25,50,75,99,99.9]))
for o in [(0,0),(0,1),(1,0),(1,1)]: print(o, px[o[0]::2,o[1]::2].mean())
R2=R//2*2
f=lambda a,b: px[a:R2:2,b::2].astype(np.float32)-64
for name,(r,g1,g2,b) in {'rggb':((0,0),(0,1),(1,0),(1,1)),'bggr':((1,1),(0,1),(1,0),(0,0)),'grbg':((0,1),(0,0),(1,1),(1,0)),'gbrg':((1,0),(0,0),(1,1),(0,1))}.items():
    rgb=np.stack([f(*r),(f(*g1)+f(*g2))/2,f(*b)],-1).clip(0)
    m=rgb.reshape(-1,3).mean(0)+1e-3; rgb*=m[1]/m
    rgb/=max(np.percentile(rgb,99.5),1)
    Image.fromarray((rgb.clip(0,1)**(1/2.2)*255).astype(np.uint8)).resize((1000,749)).save(sys.argv[2]+'_'+name+'.png')
