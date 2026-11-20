#!/bin/sh
# The front camera's pop-up, as Android drives it, for the iOS bring-up (NOTES.md "Front camera"). Run on the Mac in
# the repo folder with the phone booted into ANDROID (adb + root) and the Camera app CLOSED:
#   sh tools/android-frontcam-recon.sh       then follow what it says -> outputs/android-frontcam/
# Records, while the camera rises and falls: the motor driver's (oneplus_motor, drv8834) and the hall sensors'
# (MXM1120) sysfs values, every PMIC/TLMM GPIO's state and the PWM (LPG) settings, ten times a second, and the
# kernel log. Only reads: nothing is written to the phone except a sampling script in /data/local/tmp (removed after).
set -eu
cd "$(dirname "$0")/.."
adb get-state >/dev/null 2>&1 || { echo "no adb device: boot Android and connect USB" >&2; exit 1; }
out=outputs/android-frontcam
mkdir -p "$out"
su() { adb exec-out "su -c '$1'"; }

# where the motor and the hall sensors show themselves
su 'mount -t debugfs debugfs /sys/kernel/debug 2>/dev/null; find /sys/class /sys/devices/platform /sys/bus/i2c/devices -maxdepth 4 \( -iname "*motor*" -o -iname "*hall*" -o -iname "*mxm*" -o -iname "*drv88*" \) 2>/dev/null' \
    > "$out/sysfs-paths.txt" || true
su 'ls /sys/kernel/debug/pwm /sys/kernel/debug/pinctrl 2>/dev/null; cat /sys/kernel/debug/pwm 2>/dev/null' > "$out/pwm-before.txt" || true

cat > /tmp/fcsample.sh <<'DEV'
#!/system/bin/sh
# $1 samples, ten a second: a timestamp, the motor/hall values, the PMIC GPIOs, the PWMs
n=$1
dirs=$(cat /data/local/tmp/fcdirs 2>/dev/null)
i=0
while [ $i -lt $n ]; do
	echo "=== $(cat /proc/uptime)"
	for d in $dirs; do
		for f in $d/*; do
			[ -f "$f" ] && [ -r "$f" ] && case "$f" in *uevent|*modalias|*/power/*) ;; *) echo "$f: $(head -c 200 $f 2>/dev/null | tr '\n' ' ')";; esac
		done
	done
	grep -A14 'spmi' /sys/kernel/debug/gpio 2>/dev/null | grep -E 'gpiochip|gpio(5|8|10|11|12) '
	cat /sys/kernel/debug/pwm 2>/dev/null
	i=$((i+1))
	usleep 100000 2>/dev/null || sleep 0.1
done
DEV
adb push /tmp/fcsample.sh /data/local/tmp/fcsample.sh >/dev/null
grep -v '^$' "$out/sysfs-paths.txt" | while read -r p; do [ -n "$p" ] && echo "$p"; done | tr '\n' ' ' > /tmp/fcdirs
adb push /tmp/fcdirs /data/local/tmp/fcdirs >/dev/null
rm -f /tmp/fcsample.sh /tmp/fcdirs

step() {   # $1 name, $2 seconds, $3 what to do
	su 'dmesg -c > /dev/null'
	echo
	echo ">>> $3   (recording $2 s)"
	su "sh /data/local/tmp/fcsample.sh $(( $2 * 10 ))" > "$out/$1.txt" || true
	su 'dmesg' > "$out/$1-dmesg.txt" || true
	echo "    $(grep -c '^===' "$out/$1.txt") samples, $(wc -l < "$out/$1-dmesg.txt") log lines"
}
step down 3 "camera closed: leave it"
step rise 10 "open the Camera app NOW and switch to the FRONT camera (let it rise fully)"
step up 3 "front camera up: leave it"
step fall 8 "close the Camera app NOW (swipe it away) and let the camera go down"
su 'cat /sys/kernel/debug/gpio' > "$out/gpio-after.txt" || true
su 'rm -f /data/local/tmp/fcsample.sh /data/local/tmp/fcdirs'
echo
ls -l "$out"
