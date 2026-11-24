# Flashing Orca Seaflake onto your Teletype

This folder holds the firmware (`orca-seaflake-teletype.hex`, checksum in `orca-seaflake-teletype.hex.sha256`) and `update_firmware.command` (the three dfu-programmer commands). The app **Orca Seaflake TT** (`/Applications`) has the same firmware inside and flashes it from its Flash window (cmd+F). These are the steps the app shows; the manual (`docs/manual/Orca Seaflake TT Manual.pdf`) has them too.

## Flashing Orca Seaflake onto the Teletype


Orca Seaflake replaces the Teletype firmware. Flashing erases the whole chip: scenes, Orca patches and calibration. The bootloader itself can't be erased, so a failed flash is never fatal: put the module back in its bootloader and try again.


### What you need

- The Teletype in its case, powered from the case as usual.
- A USB A-to-A cable (the Teletype's USB port is type A; on a Mac with USB-C use a C-to-A adapter). Some A-to-A cables only carry power: if nothing is found, try another cable.
- dfu-programmer on the Mac: in Terminal, run  brew install dfu-programmer (the Flash window checks for it and can run this for you).
- The firmware: orca-seaflake-teletype.hex. It is inside the app (Orca Seaflake TT) and in dist/ next to its SHA-256 checksum.

### Before you flash: back up

1. Put a USB stick in the Teletype while it is running. The USB menu appears (on the stock firmware and on Orca Seaflake alike).
2. Turn PARAM to WRITE TO USB and press the front-panel button. It saves all the scenes as tt00s.txt - tt31s.txt (this alt firmware keeps 16: tt00s - tt15s). If Orca Seaflake is already on the module it also writes SFSCAN.TXT (the I2C scan) and SFPATCH.TXT (patches + songs), which the emulator imports with cmd+I.
3. The Disting EX and ER-301 can stay on the I2C bus while you flash.

### Step 1: put the Teletype in its bootloader

1. Switch the case off.
2. Press and hold the Teletype's front-panel button (the small SCENE RECALL button next to the USB port). Keep holding it and switch the case on. Hold for one more second, then let go.
3. Connect the Teletype's USB port to the Mac with the A-to-A cable.
4. In bootloader mode the screen stays dark. That is normal. The Mac sees an "Atmel DFU" device (USB 03eb:2ff6).

### Step 2: flash


In the app (Orca Seaflake TT, in /Applications) press cmd+F: the Flash window. It has three steps.

1. CONNECT: the status line turns from amber "Teletype not connected" to cyan "Teletype ready to flash" within two seconds of connecting the bootloader. If dfu-programmer is missing, one line says so with an INSTALL button (it runs brew install dfu-programmer).
2. SETTINGS: the addresses are fixed in the firmware: the Disting EX at 65 (0x41), the ER-301 at 0x31. On the Disting EX set Settings > I2C address = 65 (it leaves the factory at 49 = 0x31, the ER-301's address: change it first). "load Disting EX preset at power-on" (optional): tick it and type a preset number, and the module sends EX.PRESET once at every power-on, as soon as the Disting EX answers. Unticked, nothing is sent. A loadbang (`) in a patch does the same per patch.
3. FLASH ORCA SEAFLAKE: press it and confirm. A progress bar shows erase, flash and start; don't unplug anything until it says DONE (about 20 seconds).

Everything else (the pulse length, the module read-back, the firmware's size and SHA-256, the raw log) is under DETAILS, closed by default. "Restore stock Teletype..." is the way back (below).


From Terminal (what the app runs):


```sh
cd ~/Downloads     # the folder with orca-seaflake-teletype.hex (in this repository: dist/)
dfu-programmer at32uc3b0512 erase
dfu-programmer at32uc3b0512 flash orca-seaflake-teletype.hex --suppress-bootloader-mem
dfu-programmer at32uc3b0512 start
```


### Step 3: first start

1. After START the Teletype restarts by itself. Unplug the USB cable.
2. The screen says SCENES WILL BE OVERWRITTEN / PRESS ONLY IF YOU ARE UPDATING FIRMWARE. Press the front-panel button once. That prepares the scene memory (the normal Teletype first-start step); Orca's patch memory is prepared by itself.
3. The Orca Seaflake grid appears and the bottom line shows what the I2C scan found, for example FOUND DISTING EX. ctrl+I shows the Disting EX (0x41), the ER-301 (0x31), Just Friends (0x70), the bus and the message count; enter looks again.
4. To get your Teletype scenes (their scripts run with $script:N and F1-F10) back: USB stick in, turn PARAM to READ FROM USB, press the front-panel button. The grid comes back by itself.
5. Esc never leaves the grid; alt+o (option+o) switches to the stock Teletype screens and back. On the grid the front-panel button opens LOAD PATCH (PARAM picks the slot, the button again loads it). To flash again, power off and use step 1: the bootloader is only reached by holding the button at power-on.

### If something goes wrong

- NO TELETYPE BOOTLOADER / "no device present": the module isn't in its bootloader. Power off, hold the front-panel button while powering on, then connect the cable. Try another cable or USB port; adapters and hubs sometimes hide the device.
- "dfu-programmer: command not found": brew install dfu-programmer (Homebrew from brew.sh first).
- Flashing stops halfway: nothing is damaged. Put the module back in its bootloader and flash again.
- The screen stays dark after DONE: switch the case off and on (without holding the button).
- ctrl+I shows -- for the Disting EX: check its I2C cable (GND to GND at every end) and power, and that its I2C address (Settings) is 65, then press enter there. After a cold start give the ER-301 and the Disting EX time to boot: the module keeps looking for them for 2 minutes.
- The BUS line on the ctrl+I screen: PULLUPS OK is right. NO PULLUPS / NO SDA PULLUP / NO SCL PULLUP: nothing on the bus pulls that line up (the Teletype's own weak pull-ups are on, which is enough for a short bus; otherwise switch on a module's or the busboard's pull-ups). SDA HELD LOW / SCL HELD LOW: something holds the line: a reversed cable or a module stuck mid-message.
- The ER-301 line says -- TRY <P1: the ER-301 is not asked during the scan (it does not answer reads); send it anything (D4 with .<p01 one cell right below it) and it turns FOUND.
- Notes don't play but the Disting EX is FOUND: watch the bottom line while the patch runs. I2C 41 NACK CMD 6A = the Disting EX refused that message; I2C 41 BUSY = it timed out (the bus then restarts by itself). The last ctrl+I line counts OK and FAIL messages since power-on.

## Going back to the stock Teletype firmware

1. Download the latest release from github.com/monome/teletype/releases: the zip contains teletype.hex (in the Flash window, "Restore stock Teletype..." picks the downloaded .zip or .hex).
2. Put the Teletype in its bootloader exactly as in step 1 above.
3. Flash it the same way: in the Flash window press "Restore stock Teletype...", pick the file, then FLASH STOCK; or in Terminal:

```sh
cd ~/Downloads
dfu-programmer at32uc3b0512 erase
dfu-programmer at32uc3b0512 flash teletype.hex --suppress-bootloader-mem
dfu-programmer at32uc3b0512 start
```

4. First start: press the front-panel button when it asks, then READ FROM USB to restore your scenes from the backup. Orca patches are not kept by the stock firmware; SFPATCH.TXT on the stick keeps them for the emulator and for the next time you flash Orca Seaflake.

### What is erased, what is kept

- Teletype scenes: erased by flashing. Back up with WRITE TO USB, restore with READ FROM USB.
- Orca patches and songs: erased by flashing. WRITE TO USB saves them to SFPATCH.TXT.
- Teletype calibration: back to the default.
- The bootloader: never touched (--suppress-bootloader-mem).
