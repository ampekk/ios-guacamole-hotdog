#!/usr/bin/env python3
"""
The rear main camera's lens/OIS controller (ON Semi LC898124EP3) runs its program from RAM: Android downloads it on
every power-up. This takes the program out of OnePlus's GPL camera driver (research/oneplus-sm8150-camera, not in git)
into a file for the phone (not in git either): /var/local/fw/lc898124-so2820.fw, which ISP.mediacapture hands to the
kernel (HAKIOS_CAMERA_LENS_FIRMWARE) when the camera opens.

The variant is the one Android's log names for this phone: imx586_lc898124ep3_ois, SelectDownload(Gyro 2, Act 2
(SO2820), Master), table DTbl_M entry 0x0202 MODEL_0 = LC898124EP3_Code_0_1_2_2_0_0.h (Vendor 01, User 1b, Model 00,
Version 1c, LSM6DSM; verify sums PM 0xbbafb DMA 0x800650d DMB 0x2bca3b6b).

File: "LC124FW1", then little-endian u32 PM words, DMA size, DMB size, parity (the three sums added), then the PM
bytes (5 a word) and the DM bytes ((DMA + DMB) * 6 / 4).

  python3 tools/lc898124-fw.py [header.h] [out.fw]
"""
import re, struct, sys

V = '0_1_2_2_0_0'
src = sys.argv[1] if len(sys.argv) > 1 else ('research/oneplus-sm8150-camera/drivers/media/platform/msm/camera_8150_op/'
                                             'cam_sensor_module/cam_ois/ois_fw/LC898124EP3_Code_%s.h' % V)
out = sys.argv[2] if len(sys.argv) > 2 else 'outputs/camhw/lc898124-so2820.fw'
text = open(src).read()

def define(name):
    return int(re.search(r'#define\s+%s\s+(\w+)' % name, text).group(1), 0)

def array(name):
    body = re.search(r'%s\[\]\s*=\s*\{(.*?)\};' % name, text, re.S).group(1)
    body = re.sub(r'/\*.*?\*/|//[^\n]*', '', body, flags=re.S)
    return bytes(int(v, 0) for v in re.findall(r'0[xX][0-9a-fA-F]+|\d+', body))

pm_words = define('LC898124EP3_PMSize_' + V)
dma, dmb = define('LC898124EP3_DMA_ByteSize_' + V), define('LC898124EP3_DMB_ByteSize_' + V)
parity = (define('LC898124EP3_PMCheckSum_' + V) + define('LC898124EP3_DMA_CheckSum_' + V) +
          define('LC898124EP3_DMB_CheckSum_' + V)) & 0xffffffff
pm, dm = array('LC898124EP3_PM_' + V), array('LC898124EP3_DM_' + V)
dm_bytes = (dma + dmb) * 6 // 4
assert len(pm) >= pm_words * 5, (len(pm), pm_words)
assert len(dm) >= dm_bytes, (len(dm), dm_bytes)
# the DM's information record (A0 00 vendor user model version), as GetInfomationBeforeDownload finds it
info = next(dm[i + 2:i + 6] for i in range(0, len(dm), 6) if dm[i] == 0xA0 and dm[i + 1] == 0x00)
blob = b'LC124FW1' + struct.pack('<4I', pm_words, dma, dmb, parity) + pm[:pm_words * 5] + dm[:dm_bytes]
open(out, 'wb').write(blob)
print('%s: PM %u words, DMA %#x, DMB %#x, parity %#x, info %s, %u bytes; PM starts %s' % (
    out, pm_words, dma, dmb, parity, info.hex(), len(blob), pm[:10].hex(' ')))
