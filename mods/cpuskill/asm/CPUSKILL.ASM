**************************************************************
*
* CPU skill offset (mods/cpuskill/gen.txt)
*
**************************************************************

	.file	"cpuskill.asm"
	.title	"cpuskill"
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

	;Added to the drones' skill (0 to 29 in the end); set by the machine while the mod is on.
	BSSX	cpu_skill_add,16

	.text
	.even
