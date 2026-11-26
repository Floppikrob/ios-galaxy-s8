# Working on the A5XELTE fork

- For Samsung SM-A510F work, read `A5XELTE_PROGRESS.md` and `docs/a5xelte-migration-map.md` first. Record new device evidence in the former and commit completed changes. Keep private VM addresses, passwords, device dumps and downloaded Apple binaries out of git.
- Preserve recovery p11, EFS p3 and the existing Linux BOOT checkpoint. Current XNU trials may write only the explicitly authorized BOOT p10; the donor's Xiaomi `fastboot` and USERDATA instructions are not A5XELTE instructions.
- Keep build success, recovered kernel markers, physical screen reports and iOS runtime behavior as separate claims.

## Original SWEET contributor instructions (donor reference)

- Record Samsung S8+ bring-up in `docs/CONTINUE_IOS_S8_PLUS.md` and `docs/dream2lte-fts-transport.md`.
- Commit completed changes. Use `gh` for GitHub Actions kernel builds and logs.
  Local single-file cross-compiles are useful before spending a CI run.
- Device bring-up uses temporary `fastboot boot` images. Preserve the Android
  installation and partition backups. The user approved (2026-10-15) using
  internal UFS for iOS and said Android may be deleted: the iOS root goes
  into the existing `userdata` partition (no GPT changes). Keep bootloaders,
  recovery, modem, persist and the backups intact, and confirm each
  destructive step with the user before running it.
- Keep device dumps, serial numbers and downloaded Apple artifacts out of git.
