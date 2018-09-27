This repository contains utilities to use the [HM-CFG-USB(2)][] (HomeMatic USB
Konfigurations-Adapter, seems to be discontinued) from [ELV][] on Linux/Unix
by using [libusb 1.0][].

The HM-CFG-USB can be used to send and receive [BidCoS-Packets][] to control
[HomeMatic][] home automation devices (like remote controllable sockets,
switches, sensors, ...).

This repository contains, amongst others, an application, which emulates the
HomeMatic LAN configuration adapter-protocol to make it possible to use the
HM-CFG-USB in [Fhem][] or as a lan configuration tool for the [CCU][] or the
HomeMatic windows configuration software, also supporting devices using
AES-signing like [KeyMatic][].

[HM-CFG-USB(2)]: http://www.eq-3.de/Downloads/eq3/downloads_produktkatalog/homematic/bda/HM-CFG-USB-2_-UM-eQ-3-150129-web.pdf
[ELV]: http://www.elv.de/
[libusb 1.0]: http://www.libusb.org/
[BidCoS-Packets]: http://homegear.eu/index.php/BidCoS%C2%AE_Packets
[HomeMatic]: http://www.homematic.com/
[Fhem]: http://fhem.de/
[KeyMatic]: http://www.elv.de/homematic-funk-tuerschlossantrieb-keymatic-silber-inkl-funk-handsender.html
[CCU]: http://www.elv.de/homematic-zentrale-ccu-2.html

### Short hmland HowTo: ###

1.  Install prerequisites:
    `apt-get install libusb-1.0-0-dev build-essential git`
2.  Get the current version of this software (choose **one** option):
    *   Get the current *release*-version as a .tar.gz:
        1.  Download the latest version from the [releases-directory][].
            Version 0.100 is used as an example for the following commands.
        2.  Extract the archive: `tar xzf hmcfgusb-0.100.tar.gz`
        3.  Change into the new directory: `cd hmcfgusb-0.100`
    *   Get the current *development*-version via git (can be easily updated with `git pull`):
        1.  `git clone https://git.zerfleddert.de/git/hmcfgusb`
        2.  Change into the new directory: `cd hmcfgusb`
    *   Get the current *development*-version as an archive:
        1.  [hmcfgusb-HEAD-xxxxxxx.tar.gz][] (xxxxxxx is part of the commit-id.
	    xxxxxxx is just a placeholder for this HowTo, use your value)
        2.  Extract the archive: `tar xzf hmcfgusb-HEAD-xxxxxxx.tar.gz`
        3.  Change into the new directory: `cd hmcfgusb-HEAD-xxxxxxx`
3.  Build the code: `make`
4.  Optional: Install udev-rules so normal users can access the device:
    `sudo cp hmcfgusb.rules /etc/udev/rules.d/`
5.  Plug in the HM-CFG-USB
6.  Run hmland (with debugging the first time, see `-h` switch):
    `./hmland -p 1234 -D`
7.  Configure Fhem to use your new HMLAN device:  
    ``define hmusb HMLAN 127.0.0.1:1234``  
    ``attr hmusb hmId <hmId>``

**Important compatibility information:**  
If older Fhem-versions (before 2015-06-19) or [Homegear][] before 2015-07-01
is used to connect to hmland, the `-I` switch might be needed to
impersonate a LAN-interface (this replaces the identity string HM-USB-IF with
HM-LAN-IF). eQ-3 rfd (CCU and configuration software) works without this switch.
Software which needs this will not keep a stable connection open to
hmland without this switch. It was the hardcoded default in versions
< 0.100.

This incompatibility is needed so connecting software is able to
differentiate between HM-CFG-LAN and HM-CFG-USB.

**Important security information:**  
Versions before 0.101 do not correctly transmit the AES channel-mask
to the HM-CFG-USB, which results in signature-requests not being generated
by the device in most cases. This can lead to processing of unsigned messages
by the host-software. If you are relying on authenticated messages
(with e.g. aesCommReq in Fhem) from devices like door-sensors and remotes,
you should upgrade to at least version 0.101.

[releases-directory]: https://git.zerfleddert.de/hmcfgusb/releases/
[hmcfgusb-HEAD-xxxxxxx.tar.gz]: https://git.zerfleddert.de/cgi-bin/gitweb.cgi/hmcfgusb/snapshot/HEAD.tar.gz
[Homegear]: https://www.homegear.eu/

### Updating the HM-CFG-USB firmware to version 0.967: ###

1.  Compile the hmcfgusb utilities like in the hmland HowTo above
    (steps 1 to 5) and stay in the directory
2.  Download the new firmware: [hmusbif.03c7.enc][] (extracted from the
    [Firmware update tool][]):
    `wget https://git.zerfleddert.de/hmcfgusb/firmware/hmusbif.03c7.enc`
3.  Make sure that hmland is not running
4.  Flash the update to the USB-stick:
    `./flash-hmcfgusb hmusbif.03c7.enc` (You might need to use `sudo` for this)

[hmusbif.03c7.enc]: https://git.zerfleddert.de/hmcfgusb/firmware/hmusbif.03c7.enc
[Firmware update tool]: http://www.eq-3.de/Downloads/Software/Firmware%20Update%20Tool/HM-CFG-USB-2_FW-UpdateTool-Usersoftware_V1_1_eQ-3_140619.zip

### Updating HomemMatic devices over the air (OTA) (also for CUL- and HM-MOD-UART-devices): ###

1.  Compile the hmcfgusb utilities like in the hmland HowTo above
    (steps 1 to 5) and stay in the directory
2.  Download the new firmware from [eQ-3][], in this example the HM-CC-RT-DN
    firmware version 1.4
3.  Extract the tgz-file: `tar xvzf hm_cc_rt_dn_update_V1_4_001_141020.tgz`
4.  Make sure that hmland is not running
*   When using the **[HM-CFG-USB(2)][]**, flash the new firmware to the device
    with serial *KEQ0123456*:  
     `./flash-ota -f hm_cc_rt_dn_update_V1_4_001_141020.eq3 -s KEQ0123456`
*   When using a **[culfw][]**-, **[a-culfw][]**- or **[tsculfw][]**-based
    device (**[CUL][]/[COC][]/...**), flash the new firmware to the device
    with serial *KEQ0123456*:  
     `./flash-ota -f hm_cc_rt_dn_update_V1_4_001_141020.eq3 -s KEQ0123456 -c /dev/ttyACM0`
*   When using the **[HM-MOD-UART][]**, flash the new firmware to the device
    with serial *KEQ0123456*:  
     `./flash-ota -f hm_cc_rt_dn_update_V1_4_001_141020.eq3 -s KEQ0123456 -U /dev/ttyAMA0`

**Automatic firmware-updates:**  
The options `-C` (HMID of central), `-D` (HMID of device) and `-K` (AES key w/
index) can be used to send a device to the bootloader automatically without
manually rebooting the device while pressing buttons:

`./flash-ota -f hm_cc_rt_dn_update_V1_4_001_141020.eq3 -C ABCDEF -D 012345 -K 01:00112233445566778899AABBCCDDEEFF`

`-K` is only needed, when AES signing is active on the device.

**Acknowledgments:**  
flash-ota uses the public domain [AES implementation by Brad Conte][] to answer
signing-requests with culfw-devices.

[eQ-3]: http://www.eq-3.de/downloads.html
[culfw]: http://culfw.de/culfw.html
[a-culfw]: https://forum.fhem.de/index.php?topic=35064.0
[tsculfw]: https://forum.fhem.de/index.php?topic=24436.0
[CUL]: http://busware.de/tiki-index.php?page=CUL
[COC]: http://busware.de/tiki-index.php?page=COC
[HM-MOD-UART]: https://www.elv.de/homematic-funkmodul-fuer-raspberry-pi-bausatz.html
[AES implementation by Brad Conte]: https://github.com/B-Con/crypto-algorithms
