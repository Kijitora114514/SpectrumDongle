# SpectrumDongle UART Protocol v1

## UART settings

- 115200bps
- 8 data bits
- No parity
- 1 stop bit
- TX only
- 3.3V logic

## Frame

Each frame is one LF-terminated UTF-8/ASCII line. CR is not sent.

```text
SD1<TAB>sequence<TAB>layer-index<TAB>battery-0<TAB>battery-1<TAB>battery-2<TAB>battery-3<TAB>battery-4<TAB>layer-name<LF>
```

Example:

```text
SD1\t42\t2\t87\t64\t-1\t-1\t-1\tLower\n
```

Fields:

1. `SD1`: protocol magic and version
2. `sequence`: unsigned 32-bit sequence number; wraps naturally
3. `layer-index`: highest active ZMK layer index
4. `battery-0` ... `battery-4`: percentage from 0 through 100
5. `layer-name`: ZMK `display-name`, or the numeric layer index as fallback

A battery value of `-1` means that no battery report has been received for
that slot since boot. Slots follow ZMK split peripheral pairing/source order.

## Escaping

The layer-name field uses these escapes:

| Input | Encoded |
| --- | --- |
| backslash | `\\` |
| tab | `\t` |
| carriage return | `\r` |
| line feed | `\n` |

The receiver should discard bytes until it finds a line beginning with
`SD1<TAB>`. Every frame is a complete snapshot, so a missed or malformed line
can be ignored safely and the next heartbeat used instead.

