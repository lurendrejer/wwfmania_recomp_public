**************************************************************
*
* Broken record commentary (mods/brokenrecord/gen.txt)
*
**************************************************************

	.file	"brokenrecord.asm"
	.title	"brokenrecord"
	.width	132
	.option	b,d,l,t
	.mnolist

	.include	"macros.h"
	.include	"mproc.equ"
	.include	"display.equ"
	.include	"gsp.equ"
	.include	"sys.equ"
	.include	"game.equ"
	.include	"plyr.equ"
	.include	"anim.equ"

	;Non-zero while the mod is on; the machine sets it every frame.
	BSSX	brec_on,16
	;The one line the commentators say (a triple_sndtab index, 0E0h and up). 0: the next line
	;said becomes it. The game clears it when it starts, so every new game gets a new one.
	BSSX	brec_line,16

	.text
	.even
