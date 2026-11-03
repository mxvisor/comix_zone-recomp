Comix Zone for Linux (x64)
Development version

This is a static recompilation of the Windows 95 version of Comix Zone
(COMIX.EXE); it replaces the exe. The archive holds the port and a copy of the
game's CD (the game's files and its music as ogg tracks), made from the CD
image when the archive was built.
The port is not finished: expect bugs.

Libraries
---------

The game requires following 64-bit libraries: SDL2, SDL2_mixer
On debian based distributions these libraries are in following packages: libsdl2-2.0-0 libsdl2-mixer-2.0-0

The binary needs glibc 2.38 or newer.


Running
-------

Run the program in the directory the archive was unpacked into:

    ./comix-zone


Saved games
-----------

The game writes its files into the game's directory. The environment
variable CZ_WRITE_ROOT (an absolute path) puts them elsewhere; the game
then reads its own files from there first.


Configuration
-------------

The port's settings are in ComixZone.cfg in the game's directory; the file
describes them. Another file can be given with --config-file=FILE. Without
the file the game plays with sound and music.

The environment variables are:
CZ_CD_IMAGE    the CD's cue sheet, when the music is not the archive's
CZ_DATA_DIR    the game's directory, when the game is not started from it
CZ_WRITE_ROOT  where the files the game writes go (see "Saved games")
CZ_NOSOUND=1   no digital sound (=0: sound, whatever the file says)
CZ_NOMUSIC=1   no music (=0: music, whatever the file says)
CZ_SHADER=crt   a filter of the picture: smooth, pixelart, scanlines, crt (none: the plain one)


Misc
----

The window can be resized.

The game is recompiled with SR, the static recompiler: https://github.com/M-HT/SR
