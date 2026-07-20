# AGENTS.md — jw-daily-text

Instructions for AI agents (and humans) working on this repository.

## Project overview

**JW Daily Text** is a Pebble smartwatch app that displays the daily Bible text
and commentary from wol.jw.org.

- `src/c/mdbl.c` — the entire watch app (single file): UI, day cache, day
  navigation, animations, touch input, localization (en/de/it/es/fr).
- `src/pkjs/index.js` — the phone companion (JS): fetches/parses texts from
  wol.jw.org, caches in `localStorage`, background yearly import.
- `config/` — settings page + language catalogue (`languages.json`).
- `store/` — store listing assets (icons, screenshots). Icon generator output
  is committed; regenerate with PIL if the design changes.
- Target platform: **emery only** (color + touchscreen). App virtual size must
  stay **< 64 KiB** or the build fails at `inject_metadata`
  (`struct.error: 'H' format requires 0 <= number <= 65535`). Check with
  `~/.local/share/pebble-sdk/SDKs/current/toolchain/arm-none-eabi/bin/arm-none-eabi-size build/emery/pebble-app.elf`.

## Build

```sh
pebble build
```

- Version bumps happen in `package.json` (`version`, repo convention: bump on
  every release).
- The `%02d` format-truncation warning in `add_days_to_date` is pre-existing
  and harmless; the `LOAD segment RWX` linker warning is normal.

## 1. Testing with the emulator

```sh
# install & launch (reuses an already-running emulator)
pebble install --emulator emery build/jw-daily-text.pbw

# drive the UI
pebble emu-button --emulator emery click down --repeat 10 --interval 120
pebble emu-button --emulator emery click up|select|back
pebble screenshot --emulator emery --no-open /tmp/shot.png   # then Read the png
pebble logs --emulator emery                                  # watch + phone logs
```

**Data / offline testing:** the app fetches from `wol.jw.org`. If the network
blocks it (or you want deterministic tests), the pkjs `localStorage` cache is
used without any network. It lives at
`~/.local/share/pebble-sdk/<sdk>/emery/localstorage/<app-uuid>.dat`
(fixed 1024-byte slots) with a `<uuid>.dir` index file of lines
`'dt.<locale>:<yyyy-mm-dd>', (<offset>, <len>)`. A large real cache is already
present in the dev environment — don't delete it.

- `KNOWN_RSCONF` in `src/pkjs/index.js` maps locales to known rsconf values;
  for unlisted locales `findRsconf` probes r1..r20 (5 s timeout each ≈ 100 s
  when offline). Expect that delay on first fetch for new languages.
- The phone side starts a **yearly import** on boot and floods the watch with
  every cached day; that's normal.
- Inject AppMessages into the running watch app with `pebble repl` (the CLI's
  `send-app-message` does not reach the app on the emulator bridge). Example —
  switch UI language to German (keys: action=10000, language=10006, lib=10007,
  cache_days=10016, rsconf=10017; see `build/src/message_keys.auto.c`):

  ```sh
  timeout 40 pebble repl --emulator emery <<'EOF'
  import uuid as uuid_module
  from libpebble2.services.appmessage import AppMessageService, Int32, CString
  service = AppMessageService(pebble)
  service.send_message(uuid_module.UUID("ecca2ec3-068b-4cff-a500-a27184c1b841"),
      {10000: Int32(4), 10006: CString("de"), 10007: CString("lp-x"),
       10017: CString("10"), 10016: Int32(7)})
  EOF
  ```

- **Touch cannot be emulated**: `emu-tap` is the accelerometer, there is no
  touch-injection API. Verify touch changes by code review + on the physical
  device.
- `pebble kill` stops all emulators.

## 2. Deploying to the developer's test device

The developer's watch is paired at **192.168.178.110** (LAN; ask if it changed):

```sh
pebble ping --phone 192.168.178.110
pebble install --phone 192.168.178.110 build/jw-daily-text.pbw
pebble logs --phone 192.168.178.110        # optional
```

## 3. Publishing a new version

Store: Rebble appstore (`https://appstore-api.repebble.com`), app id
`672b1221aeef4d30897a361c`, page
<https://apps.repebble.com/672b1221aeef4d30897a361c>, category **daily**.

```sh
# one-time / when logged out: opens a browser OAuth flow (needs a human)
pebble login && pebble login --status

# bump package.json version first, then:
pebble build
pebble publish --non-interactive \
  --name "JW Daily Text" \
  --version "$(python3 -c 'import json;print(json.load(open("package.json"))["version"])')" \
  --release-notes "<what changed>" \
  --description "Read today's Bible text and commentary on your Pebble. Works offline, preselects your watch language, with color UI, smooth day animations and touch scrolling." \
  --source "https://github.com/testarossa47/jw-daily-text" \
  --category daily \
  --icon-small store/icon_small.png \
  --icon-large store/icon_large.png \
  --screenshots store/screenshots/emery_today.png \
               store/screenshots/emery_next_day.png \
               store/screenshots/emery_german.png \
  --no-gif-all-platforms
```

Notes:

- Store icons: `icon_small.png` = **80×80**, `icon_large.png` = **144×144**.
  Screenshot filenames must start with the platform (`emery_…`).
- Releases are created as drafts unless `--is-published` is passed; publish
  them from the dashboard (`https://appstore-api.repebble.com/dashboard`).
  Publish only the newest release.
- The store listing **title follows the pbw `displayName`** of the newest
  release; the developer name is set once via
  `POST /api/v1/developer/create {"name": "testarossa47"}` with the Firebase
  bearer token (the dashboard also works).
- Store screenshots are taken from the emulator (see section 1); take them
  with the release build and copy into `store/screenshots/`.

## Git workflow

- Commit per logical change; reference the GitHub issue number in the message.
- Ask before `git push`/closing issues (the user may want to review first).
- Close issues with `gh issue close <n> --comment "Implemented in <sha>: …"`.
