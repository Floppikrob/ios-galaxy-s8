param([Parameter(Mandatory)][string]$Image)
# Write an iOS BOOT image to a Galaxy S8 (SM-G950F) in TWRP and reboot.
# Writes only the BOOT partition. Refuses to write unless TWRP reports the
# S8 (dreamlte), the by-name link BOOT exists and the image fits in it.
# After writing it reads the image back and compares SHA-256.
# Usage (phone in TWRP, USB connected): tools\dreamlte\flash-boot.ps1 s8-ios-boot.img
$ErrorActionPreference = 'Stop'
function Fail($msg) { Write-Host "STOP: $msg" -ForegroundColor Red; exit 1 }

if (-not (Test-Path $Image)) { Fail "no file $Image" }
$size = (Get-Item $Image).Length
$hash = (Get-FileHash -Algorithm SHA256 $Image).Hash.ToLower()
Write-Host "image: $Image, $size bytes, sha256 $hash"

$state = (adb get-state 2>$null)
if ($state -ne 'recovery') { Fail "the phone is not in TWRP (adb state: '$state')" }
$device = (adb shell getprop ro.product.device).Trim()
if ($device -notmatch 'dreamlte') { Fail "TWRP reports device '$device', not dreamlte" }
$boot = (adb shell 'readlink -f /dev/block/by-name/BOOT 2>/dev/null || readlink -f /dev/block/platform/*/by-name/BOOT').Trim()
if ($boot -notmatch '^/dev/block/sd[a-z]+[0-9]+$') { Fail "BOOT partition not found (got '$boot')" }
$part = [int64](adb shell "blockdev --getsize64 $boot").Trim()
if ($size -gt $part) { Fail "image ($size) is larger than BOOT ($part)" }
Write-Host "BOOT = $boot ($part bytes)"

adb push $Image /tmp/boot.img | Out-Null
$remote = (adb shell 'sha256sum /tmp/boot.img').Trim().Split(' ')[0]
if ($remote -ne $hash) { Fail "copy on the phone differs ($remote)" }
adb shell "dd if=/tmp/boot.img of=$boot bs=1048576 conv=fsync && sync" | Out-Null
$back = (adb shell "head -c $size $boot | sha256sum").Trim().Split(' ')[0]
if ($back -ne $hash) { Fail "readback differs ($back): restore your BOOT backup before rebooting" }
Write-Host "written and verified; rebooting" -ForegroundColor Green
adb reboot
