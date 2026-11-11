Centrifuge XZ Tools
===================

Work in progress towards the Centrifuge loader for the XDJ-XZ (firmware 1.26).

Right now it does one thing: double-click "Centrifuge XZ Tools.exe" and it

  1. downloads the official XDJ-XZ 1.26 firmware from AlphaTheta (about 70 MB)
  2. downloads Pioneer's published XDJ-XZ source archive (about 250 MB, for the boot support file)
  3. checks both, and writes  Desktop\Centrifuge XZ\xz-1.26-player.zip  (the player program and its files)

Nothing is written to the XDJ-XZ or to a USB stick. Downloads are kept in %LOCALAPPDATA%\Centrifuge\xz
so a second run is quick.

Credits
-------
The firmware download, boot support and decryption steps follow the XZ Mods builder by OpticMystic
(https://github.com/OpticMystic/xdj-xz-toolkit, MIT licence). Thank you.
