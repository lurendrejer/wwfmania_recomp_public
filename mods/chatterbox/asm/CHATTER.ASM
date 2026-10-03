**************************************************************
*
* Chatterbox commentary (mods/chatterbox/gen.txt)
*
**************************************************************

	.file	"chatterbox.asm"
	.title	"chatterbox"
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
	BSSX	chatter_on,16

	.text
	.even
