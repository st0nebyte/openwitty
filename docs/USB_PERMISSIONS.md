# USB permissions and access errors

This documents the *permission* side of USB access. The driver-ownership side is in
`docs/USB_ACCESS.md`. The application never uses `sudo`, never detaches kernel drivers, never
unbinds interfaces and never resets the device on its own. Hardware writes are disabled.

## Three different failures

| Symptom (`lewitt-cli` exit code)       | Meaning | Fixed by the udev rule? |
| -------------------------------------- | ------- | ----------------------- |
| `EACCES` / permission denied (3)       | `/dev/bus/usb/BBB/DDD` is `root:root 0664`; the user may not open it for writing (usbfs control transfers need a writable fd). | **Yes** |
| `EBUSY` / resource busy (4)            | `snd-usb-audio` owns the AudioControl interface; interface-recipient requests must claim it. | **No** – a driver question, not a permission one |
| `EPIPE` / stall (5)                    | The device rejected the request (unsupported on this firmware/recipient). | No |

If `lewitt-cli diagnose` reports EACCES, install the rule below. If it then reports EBUSY the
permission problem is solved and what remains is covered by `docs/USB_ACCESS.md` (recommended
long-term route: a kernel quirk + hwdep interface, not implemented here).

## The udev rule

`packaging/udev/70-lewitt-connect2.rules` (installed to `/usr/lib/udev/rules.d/` by the
packages):

```
SUBSYSTEM=="usb", ENV{DEVTYPE}=="usb_device", ATTR{idVendor}=="29c2", TAG+="uaccess"
```

* `TAG+="uaccess"` makes logind add an ACL for the user at the active seat; no group
  membership and no `MODE="0666"`. Other users and remote sessions get nothing.
* The file name must sort **before** `73-seat-late.rules` (which turns the tag into an ACL).
  A `99-…` name is applied too late and silently does nothing — hence `70-`.
* The rule is vendor-scoped (Vendor ID 0x29c2 is LEWITT's) and affects only the USB device
  node's permissions. It does not load/bind drivers or alter the sound card.

## Installing it manually (administrator step, not done automatically)

```sh
sudo install -m 0644 packaging/udev/70-lewitt-connect2.rules /etc/udev/rules.d/
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=29c2   # or replug
```

Removal: `sudo rm /etc/udev/rules.d/70-lewitt-connect2.rules` and reload. Packages (`.deb`,
PKGBUILD) install the same file into `/usr/lib/udev/rules.d/` and the `.deb` postinst reloads udev.

## Verifying

```sh
lewitt-cli devices                       # lists the device, node path and owner/mode
getfacl /dev/bus/usb/BBB/DDD             # expect user:<you>:rw- after the rule applied
lewitt-cli diagnose                      # read-only report with next steps
```

User-local installs (`--prefix ~/.local`) do not ship the rule: udev only reads system
directories. There is no safe unprivileged substitute.
