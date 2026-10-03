**************************************************************
*
* Gravity (mods/moongravity/gen.txt)
*
**************************************************************

	.file	"moongravity.asm"
	.title	"moongravity"
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

	;The gravity a wrestler gets on each new animation (16.16, the game's is GRAVITY = 8000h);
	;0 = the game's own. The machine sets it every frame while the mod is on.
	BSSX	mg_gravity,32

	.text
	.even
