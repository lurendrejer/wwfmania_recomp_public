**************************************************************
*
* The referee mod's own data (mods/referee/gen.txt)
*
**************************************************************

	.file	"refmod.asm"
	.title	"referee mod"
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

	;Non-zero: WRESTLE.ASM creates the referee (wrestler number 9) when the
	;wrestlers are created. The machine sets it while the mod is switched on.
	BSSX	ref_enabled,16

	;The referee's process (0 when there is none); he is not in process_ptrs,
	;so the rest of the game does not see him as a wrestler.
	BSSX	ref_process,32

	.text
	.even

******************************************************************************
* The referee wants to stand where he does not block the camera between two
* wrestlers (REF.ASM calc_unhappiness). The original took process_ptrs[0] and
* [1], which are the two players; in a one-player game slot 1 is empty and the
* drone is in slot 2. This finds the first two wrestlers that are there (the
* referee himself excluded).
*
* <a4,a5 = x,z of the first, a6,a7 = x,z of the second (the first again when
* there is only one). Trashes a14.

 SUBR	ref_wrestlers

	PUSH	a0,a1,a2,a3

	movi	process_ptrs,a0
	movk	NUM_WRES,a2
	clr	a3			;wrestlers found
#lp	move	*a0+,a1,L
	jrz	#nxt
	move	*a1(PLYR_TYPE),a14
	cmpi	PTYPE_REFEREE,a14
	jreq	#nxt
	move	a3,a3
	jrnz	#second

	move	*a1(OBJ_XPOSINT),a4
	move	*a1(OBJ_ZPOSINT),a5
	move	a4,a6
	move	a5,a7
	movk	1,a3
	jruc	#nxt

#second	move	*a1(OBJ_XPOSINT),a6
	move	*a1(OBJ_ZPOSINT),a7
	jruc	#done

#nxt	dsj	a2,#lp

#done	PULL	a0,a1,a2,a3
	rets
