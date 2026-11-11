Centrifuge XZ Tools
===================

Groundwork for the Centrifuge loader on the XDJ-XZ (firmware 1.26 only).

Double-click "Centrifuge XZ Tools.exe" for a menu:

  1. Extract the XDJ-XZ 1.26 player program
     Downloads the official 1.26 firmware (AlphaTheta) and Pioneer's published source archive (~250 MB,
     first time only, then cached), checks both, and writes  Desktop\Centrifuge XZ\xz-1.26-player.zip.
     Attach that zip in the Claude chat.

  2. Write a boot TEST loader to a USB stick
     Asks for a USB drive (e.g. E:\) and writes autoexec.bin to it. This is a safe proof-of-life test:
       - Put the stick in the XZ while it's OFF, switch on, let it boot (it boots as normal), switch off.
       - Put the stick back in the PC and look for CENTRIFUGE-BOOT-OK.txt - send that to Claude.

  3. Write a RECON loader to a USB stick
     Like the test loader, but it also gathers information Claude needs to build the stem features:
       - Put the stick in the XZ while it's OFF, switch on, let it finish booting.
       - When the normal screen is up, slowly sweep the CHANNEL 3 and CHANNEL 4 knobs (TRIM, HI, MID,
         LOW) across their range, back and forth, for about a minute.
       - Switch off, put the stick in the PC, and send Claude the "Centrifuge-XZ-recon" folder (zip it).

  Both only read information. They do NOT change the player, the firmware, or anything on the XZ, and run
  from RAM only - boot without the stick and the XZ is completely standard. The tool won't overwrite an
  existing autoexec.bin unless you tell it to.

Use a spare FAT32 stick for the test and keep your normal music stick aside.

Downloads are cached in %LOCALAPPDATA%\Centrifuge\xz. Windows may warn that the exe is unsigned
(More info -> Run anyway).

Credits
-------
The firmware download, boot key and image authoring follow the XZ Mods builder by OpticMystic
(https://github.com/OpticMystic/xdj-xz-toolkit, MIT licence). Thank you.
