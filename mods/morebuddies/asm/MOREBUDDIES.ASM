**************************************************************
*
* Two buddies for each player (mods/morebuddies/gen.txt)
*
**************************************************************

	.file	"morebuddies.asm"
	.title	"more buddies"
	.width	132
	.option	b,d,l,t
	.mnolist

	.include	"macros.h"

	;Buddies for each player: 1 is the original, 2 gives each side a second
	;one (0 = the mod is off). The machine sets it while the mod is on.
	BSSX	more_buddies,16

	.text
	.even
