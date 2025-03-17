# SimU8or

A frontend framework for [SimU8](https://github.com/LifeEmu/SimU8), a portable U8 emulator written in C.

This frontend mainly focuses on emulating CASIO fx-ES (PLUS)/CWI/CWII calculators.


## Features

- **Minimum dependency.** It relies on nearly nothing. If you're willing to do so, you could run it on bare metal embedded platforms.
- **Maximum control.** You are the one to decide what to implement and what not to. In other words, you're responsible to implement everything.


## Examples

- Terminal/Console
- GTK/Win32 GUI
- SDL
- Nuklear/Dear ImGui
- bitmap frame buffer (on embedded devices)


## About this branch
**This branch contains a sketched-up SDL2 implementation of a frontend.** It emulates **CASIO fx-ES PLUS** hardware.

It was written before the framework, so it didn't use the framework, and is only included here for convenience.

Tested with MSYS2 UCRT64/CLANG64, WSL Debian and Termux (on arm64).

**Prerequisites:**
- A C toolchain, preferably `gcc` or `clang`, as they're widely-used and tested;
- GNU `make`;
- `pkg-config`
- `SDL2`
- `SDL2_ttf`, version 2.0.18 and above.

**Building:**
1. **Clone the repository and switch to this branch;**
2. **Edit the `Makefile` to match your needs.** You can change the compiler, flags, executable name, etc.;
3. Run `make`;
4. If it succeeds, **run the output executable** with `./SimU8or`.
If it does not build for you, you can report the problem. **Please attach the terminal output**, and tell me what modifications did you do to the files.

**Using:**
- It requires a ROM file, named `rom_esp.bin`, to be in the executable's directory.
- It also requires a font file, named `font.ttf`, to be in the executable's directory. It can be any font you like, but an monospace font is recommended.

**Keys:**
- **C**: Clear/Reset the core, as if [ON] is pressed
- **W**: Save state, the file will be called `state_esp.sav`
- **E**: Load state
- **M**: Inspect memory
- **P**: Poke memory
- **R**: Resume from HALT/STOP/single stepping
- **S**: Single-step
- **J**: Slow down the core
- **K**: Speed up the core
- **L**: Restore default core speed


### Known issues

- The emulated keyboard has no key labels. It's because I wrote the code for keyboard emulation before importing `SDL2-ttf`, so I had no way to add text on the keys. It's just a KI/KO matrix, and here's a table of key layout (tab size = 8):
```
keyboard matrix: (note: KI is inverted so 0FFh = no key)
	KOL
!KIL	0	1	2	3	4	5	6
7	SHIFT	ALPHA	up	right	MODE
6	CALC	Integr	left	down	x^-1	log_[]
5	frac	sqrt	x^2	x^[]	log(	ln(
4	(-)	dms	hyp	sin	cos	tan	0
3	RCL	ENG	(	)	S<>D	M+	.
2	7	8	9	DEL	AC		x10^x
1	4	5	6	*	/		Ans
0	1	2	3	+	-		=
```
- I did not handle touch screen events at all, so it will become very laggy if you swipe in the window. This problem does not occur when using a mouse.
- It uses console/terminal for most text I/O, which means you have to use the console to inspect/poke memory.
- In my WSL Debian, SDL2 requires `libasound.a` for static linking, which is not available. I am not sure if this problem exists in other systems. If you meet problems, make sure to **_NOT_** compile it as a static binary.
