**************************************************************
*
* Wrong names (mods/wrongnames/gen.txt)
*
**************************************************************

	.file	"wrongnames.asm"
	.title	"wrongnames"
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
	BSSX	wn_on,16
	;For each wrestler number (0-8), the wrestler whose name is said. The machine writes it.
	BSSX	wn_map,9*16

	.text
	.even

;a14 = a wrestler number: the one the announcer names instead (a14 unchanged when the mod
;is off or the number is out of range).
 SUBR	wn_name
	PUSH	a0
	move	@wn_on,a0
	jrz	#out
	cmpi	8,a14
	jrhi	#out
	sll	4,a14
	addi	wn_map,a14
	move	*a14,a14
#out
	PULL	a0
	rets
