# mtrx

**Transmit and receive audio via UDP unicast or multicast, using the Opus codec.**

**Copyright (C) 2014-2017 Vittorio Gambaletta**

## mtx
```
Usage: mtx [<options>]

    -h <addr>   IP address (default: 239.48.48.1)
    -p <port>   UDP port (default: 1350)
    -d <dev>    ALSA device name, or '-' for stdin (default: 'default')
    -R <n>      RTP output (default: 0)
    -f <n>      Use float samples (1) or signed 16 bit integer samples (0) (default: 0)
    -r <rate>   Audio sample rate (default: 48000 Hz)
    -c <n>      Audio channel count (default: 2)
    -t <ms>     Audio packet duration (default: 20 ms)
    -k <kbps>   Network bitrate (default: 128 kbps)
    -b <n>      ALSA buffer multiplier (default: 3)
    -T <n>      Enable or disable time synchronization (default: 1)
    -v <n>      Be verbose (default: 0)
```

## mrx
```
Usage: mrx [<options>]

    -h <addr>   IP address (default: 239.48.48.1)
    -p <port>   UDP port (default: 1350)
    -d <dev>    ALSA device name, or '-' for stdin/stdout (default: 'default')
    -f <n>      Use float samples (1) or signed 16 bit integer samples (0) (default: 0)
    -r <rate>   Audio sample rate (default: 48000 Hz)
    -c <n>      Channels count (default: 2)
    -t <ms>     Audio packet duration (default: 20 ms)
    -b <n>      ALSA buffer multiplier (default: 3)
    -e <ms>     Audio total delay (default: 80 ms)
    -T <n>      Enable or disable time synchronization (default: 1)
    -v <n>      Be verbose (default: 0)
```

## Quick 'n' easy steps to transmit audio routed from PulseAudio

- First clone the repo and run **`make`** ;)

### Transmitter

#### First time

- Append this to **`/etc/asound.conf`**:
```
pcm.pnm {
	type pulse
	device null.monitor
}
```

#### Every time

- Run **`pacmd load-module module-null-sink`** (once per session)
- Run **`sudo ./mtx -d pnm -f 1`** (the root privs are needed to get realtime priority)
- Change network bandwidth with **`-k`** if needed
- Run **`pavucontrol`** and move streams that need to be streamed to the **`Null Output`** sink
- Run **`pacmd unload-module module-null-sink`** at the end if you want

### Receiver(s)

- Run **`sudo ./mrx`** (the root privs are needed to get realtime priority)
- Change receiving latency with **`-e`** if needed
- If having problems try **`sudo ./mrx -d pulse`**
- On OpenWrt and/or with cheap USB audio cards without PulseAudio, if it doesn't work try **`mrx -d plughw:0,0`**
- It shouldn't be needed anymore, but it might still be useful, so [this is a working `/etc/asound.conf` file for OpenWrt with cheap USB audio cards](https://gist.github.com/VittGam/ad0c1ce0143e4fb7a55fe8947b085e26)

### RTP

RTP output is useful for transmitting to other applications, e.g. FFmpeg.
You don't need any PulseAudio integration; just run mtx with -R 1 and
-d etc. as usual. This will output an SDP file, which you can run in e.g.
ffplay with

```
ffplay -protocol_whitelist rtp,file,udp -i mtx.sdp
```

## Bugs

- Well, all the desync bugs seem to happen (and needed a resync hack in `mtx`) only when using `alsa-pulse` to capture from the null output sink monitor...
- If you find any bugs, please report them! :)

## TODO

- Support IPv6
- Implement native PulseAudio interface (but only if it doesn't bloat the program! The target is embedded systems like OpenWrt routers...)
- OpenWrt/LEDE packaging
- On OpenWrt, `libopus` is compiled with floating point enabled by default, and since floating point is emulated on most routers' CPUs, it's SLOW as hell. Maybe send a patch to LEDE (if that was not already done in the meantime)?
- Any suggestion?

## License

This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program. If not, see <http://www.gnu.org/licenses/>.
