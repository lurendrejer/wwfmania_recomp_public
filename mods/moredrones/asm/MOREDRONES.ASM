**************************************************************
*
* More computer opponents in a one-player or cooperative match
* (mods/moredrones/gen.txt)
*
**************************************************************

	.file	"moredrones.asm"
	.title	"more drones"
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

	.ref	RNDRNG0

	;How many computer opponents a match should have (0 = the mod is off).
	;The machine sets it while the mod is switched on.
	BSSX	more_drones,16

	;The match's own number of opponents, saved while the extra ones are there (0 = none
	;were added). NUM_OPPS is put back to it when the match is over.
	BSSX	more_orig,16

	.text
	.even

******************************************************************************
* The lineup (a4) names one wrestler per byte, the first one for the ladder
* and the others only in the final battle. Fills the bytes from opponent
* number a3 (the ladder's count) up to more_drones with random wrestlers.
*
* >a3 = opponents the ladder has, a4 = lineup
* <a4 = lineup with the extra ones filled in
* Trashes a0, a1, a2, a14 (a3 is kept)

 SUBR	more_fill_lineup

	PUSH	a3,a5,a6
	move	@more_drones,a5
	move	a3,a2
	sll	3,a2			;bit shift of opponent number a3
	movi	0FFFFh,a0
	calla	RNDRNG0			;one 16-bit random number; three bits per wrestler
	move	a0,a6			;(back-to-back calls give alike numbers)
#lp	cmp	a5,a3
	jrge	#done
	move	a6,a0
	andi	7,a0			;0-7 (7 becomes 8 in the lineup code)
	srl	3,a6
	movi	0FFh,a1
	sll	a2,a1
	andn	a1,a4			;clear that opponent's byte
	sll	a2,a0
	or	a0,a4			;...and put the new wrestler in
	addk	8,a2
	inc	a3
	jruc	#lp
#done	PULL	a3,a5,a6
	rets
