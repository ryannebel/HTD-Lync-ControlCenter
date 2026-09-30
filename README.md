# HTD-Lync-ControlCenter

HTD Lync Control Center provides a built-in web UI and HTTP API for:

- 12-zone control (power, mute, DND, source, volume, treble, bass)
- Zone/source naming
- Per-zone source visibility filtering
- Ethernet network configuration
- RS232 diagnostics and raw command testing
- Live zone updates over WebSocket

## Base URL

- `http://<device-ip>/`

## Response conventions

- Most successful API responses return HTTP `200` with JSON containing `"ok": true`.
- Validation/command failures typically return HTTP `400` with:
  - `{ "ok": false, "message": "..." }`

## Web pages

- `/` - Control Center
- `/names` - Name Manager and source visibility by zone
- `/ethernet` - Ethernet settings
- `/rs232` - RS232 test/diagnostics

### Control page query flags

The Control page supports URL query parameters:

- `showAll=1` (also accepts `true` / `yes`)
  - Shows all controls on all zone cards.
  - Without this flag, cards are collapsed by default (Power + Volume), and tap-to-expand allows one expanded card at a time.

- `mask=<int>`
  - Visibility bitmask for page elements.
  - Bits:
	- `0`: Title
	- `1`: Refresh button
	- `2`: Query All button
	- `3`: Names + Ethernet buttons
	- `4`: RS232 button
	- `5`: Log panel
	- `6..17`: Zones 1..12

Example:

- `http://<device-ip>/?mask=262143&showAll=1`

Mask example (show only zones 1, 2, 3, and 4):

- Zone bits are `6..17`, so zones 1..4 are bits `6, 7, 8, 9`.
- Mask value = `(1<<6) + (1<<7) + (1<<8) + (1<<9)`
- Mask value = `64 + 128 + 256 + 512 = 960`
- URL: `http://<device-ip>/?mask=960`

## API routes

## Status

### `GET /api/status`

Returns current cached state for all zones.

Response shape:

- `ok`: boolean
- `zones`: array of objects with:
  - `zone` (1..12)
  - `known` (bool)
  - `powerOn` (bool)
  - `muteOn` (bool)
  - `dndOn` (bool)
  - `input` (1..18)
  - `volumeDb` (int)
  - `trebleDb` (int)
  - `bassDb` (int)
  - `balanceDb` (int)

---

## Names and source visibility

### `GET /api/names`

Returns all zone names, source names, and per-zone source visibility masks.

Response shape:

- `ok`
- `zones[]`:
  - `zone`
  - `full`
  - `short`
  - `sourceMask` (18-bit integer mask for visible sources; bit0=source1 ... bit17=source18)
- `sources[]`:
  - `source`
  - `full`
  - `short`

### `GET /api/name/zone?zone=<1..12>`

Returns one zone full name.

### `GET /api/name/source?source=<1..18>`

Returns one source full name.

### `GET|POST /api/names/zone?zone=<1..12>&full=<text>&short=<text>`

Sets a zone full/short name.

- `short` is truncated to max 10 characters.

### `GET|POST /api/names/source?source=<1..18>&full=<text>&short=<text>`

Sets a source full/short name.

- `short` is truncated to max 10 characters.

### `GET|POST /api/names/zone/sources?zone=<1..12>&mask=<int>`

Sets visible sources for one zone.

- Mask uses source index bits without remapping IDs:
  - bit0 => source 1
  - bit1 => source 2
  - ...
  - bit17 => source 18

---

## Zone commands

All command endpoints accept `GET` or `POST`.

### `/api/cmd/power?zone=<1..12>&on=<0|1>`

Power off/on zone.

### `/api/cmd/mute?zone=<1..12>&on=<0|1>`

Mute off/on zone.

### `/api/cmd/dnd?zone=<1..12>&on=<0|1>`

Do-not-disturb off/on zone.

### `/api/cmd/input?zone=<1..12>&input=<1..18>`

Select source input by source index.

### `/api/cmd/volume?zone=<1..12>&level=<0..59>`

Set volume by level.

### `/api/cmd/volume?zone=<1..12>&db=<-59..0>`

Set volume using dB input.

### `/api/cmd/treble?zone=<1..12>&db=<-10..10>`

Set treble dB.

### `/api/cmd/bass?zone=<1..12>&db=<-10..10>`

Set bass dB.

### `/api/cmd/balance?zone=<1..12>&db=<int>`

Set balance (passed through to firmware command).

### `/api/cmd/query`

Triggers query of all zone statuses.

Some command responses include low-level transmission metadata:

- `txFrame`
- `txFollowUp`

---

## Network configuration

### `GET /api/network/ethernet`

Returns current Ethernet configuration and active interface details.

Typical fields used by the web UI:

- `active`
- `deviceIp`
- `dhcp`
- `ip`
- `gateway`
- `subnet`
- `dns1`
- `dns2`

### `GET|POST /api/network/ethernet?dhcp=<0|1>&ip=<...>&gateway=<...>&subnet=<...>&dns1=<...>&dns2=<...>`

Sets Ethernet config and applies restart behavior.

- When `dhcp=1`, static fields may be omitted.
- When `dhcp=0`, static addressing fields should be provided.

### `GET|POST /api/network/wifi/clear`

Clears saved Wi-Fi credentials and triggers restart behavior.

---

## RS232 test/diagnostics

### `GET|POST /api/rs232/state`

Gets/sets pause mode.

Optional argument:

- `paused=1|0` (or `true|false`) to change mode.

Response:

- `ok`
- `paused` (bool)

### `GET|POST /api/rs232/send?bytes=<hex tokens>`

Sends raw bytes to UART.

`bytes` accepts hex tokens separated by spaces/commas/newlines, for example:

- `0x02 00 01 04 57 5E`
- `02,00,01,04,57,5E`

Response includes:

- `ok`
- `written`
- `length`
- `tx` (hex string)

### `GET /api/rs232/log`

Returns captured RS232 log.

Response:

- `ok`
- `paused`
- `log`

### `GET|POST /api/rs232/log/clear`

Clears RS232 log.

---

## WebSocket push (live updates)

If WebSocket support is enabled, the device serves WS on port `81`:

- `ws://<device-ip>:81/`

Message types:

- Zone update:
  - `{ "type": "zone", "data": { ...zone fields... } }`
- Informational event:
  - `{ "type": "info", "message": "..." }`
