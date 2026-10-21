param([string]$Out = 'boot-template.img')
# Save the small part of the S8 BOOT partition that make-boot.sh needs from
# the stock image: the boot.img header page and the Samsung DTBH "dt"
# section. The result is a valid boot.img with empty kernel and ramdisk
# (usually well under 1 MB), usable as STOCK_BOOT for make-boot.sh.
# Reads only; nothing on the phone is written except a file in /tmp.
# Usage (phone in TWRP, USB connected): tools\dreamlte\get-boot-template.ps1
$ErrorActionPreference = 'Stop'
function Fail($msg) { Write-Host "STOP: $msg" -ForegroundColor Red; exit 1 }

if ((adb get-state 2>$null) -ne 'recovery') { Fail 'the phone is not in TWRP' }
$boot = (adb shell 'readlink -f /dev/block/by-name/BOOT 2>/dev/null || readlink -f /dev/block/platform/*/by-name/BOOT').Trim()
if ($boot -notmatch '^/dev/block/sd[a-z]+[0-9]+$') { Fail "BOOT partition not found (got '$boot')" }
adb shell "dd if=$boot of=/tmp/boot-full.img bs=1048576 2>/dev/null" | Out-Null
$full = Join-Path ([IO.Path]::GetTempPath()) 'boot-full.img'
adb pull /tmp/boot-full.img $full | Out-Null
adb shell 'rm -f /tmp/boot-full.img' | Out-Null

$b = [IO.File]::ReadAllBytes($full)
Remove-Item $full
if ([Text.Encoding]::ASCII.GetString($b, 0, 8) -ne 'ANDROID!') { Fail 'BOOT has no boot.img header' }
$ksz = [BitConverter]::ToUInt32($b, 8)
$rsz = [BitConverter]::ToUInt32($b, 16)
$ssz = [BitConverter]::ToUInt32($b, 24)
$page = [BitConverter]::ToUInt32($b, 36)
$dtsz = [BitConverter]::ToUInt32($b, 40)
function Pages([int64]$n) { [int64][math]::Ceiling($n / $page) * $page }
$off = $page + (Pages $ksz) + (Pages $rsz) + (Pages $ssz)
if ($dtsz -eq 0 -or $off + $dtsz -gt $b.Length -or
    [Text.Encoding]::ASCII.GetString($b, $off, 4) -ne 'DTBH') { Fail 'no DTBH section in BOOT' }

$t = New-Object byte[] ($page + $dtsz)
[Array]::Copy($b, 0, $t, 0, $page)
[Array]::Copy($b, $off, $t, $page, $dtsz)
foreach ($field in 8, 16, 24) { [Array]::Clear($t, $field, 4) }   # kernel, ramdisk, second sizes = 0
[IO.File]::WriteAllBytes((Join-Path (Get-Location) $Out), $t)
Write-Host "saved $Out ($($t.Length) bytes): send this file" -ForegroundColor Green
