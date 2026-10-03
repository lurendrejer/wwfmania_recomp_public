**************************************************************
*
* Wins needed for the match (mods/rounds/gen.txt)
*
**************************************************************

	.file	"rounds.asm"
	.title	"rounds"
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

	;Wins needed to win a match (0 = the original rule, two). Set by the machine while
	;the mod is switched on. rn_real: the real number of rounds won by each side.
	BSSX	rounds_needed,16
	BSSX	rn_real,16*2

	.text
	.even
