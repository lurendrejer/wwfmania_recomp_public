**************************************************************
*
* The Dink mod's own data and code (mods/dink/gen.txt): Doink drawn smaller
*
**************************************************************

	.file	"dinkmod.asm"
	.title	"dink mod"
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
	.include	"dinkimg.tbl"

	.ref	xsq_smallp

	;Non-zero while the mod is on (the machine sets it every frame).
	BSSX	dink_enabled,16

;how big he is: offsets times DINK_MUL / 256; the DMA's scale (8.8, larger is smaller) is
;256 * 256 / DINK_MUL
DINK_MUL	equ	128		;half his size
DINK_SCALE	equ	512

	.text
	.even

;his mugshot (one piece) for his select square (mod.c)
 SUBR	dink_mug
	.long	DIMUG,0

;his names where the life bar and the end of a round take Doink's from a table
 SUBR	dink_namep
	.long	NAM_DINK
 SUBR	dink_bigp
	.long	BIGDINK


******************************************************************************
* a14 = player number: Z clear when he is Dink (the small one). Trashes a14.

 SUBR	dink_small_p

	PUSH	a0
	move	@dink_enabled,a0
	jrz	#no1
	move	@xsq_smallp,a0
	btst	a14,a0
	jrz	#no1
	PULL	a0			;(a move: it sets the flags, so they are set again below)
	movk	1,a14
	move	a14,a14			;Z clear
	rets
#no1	PULL	a0
	clr	a14			;Z set
	rets


******************************************************************************
* ANIM.ASM #plot_object, a13 = the wrestler, a8 = the object, a1 = the
* image's x offset: smaller for Dink, and the object's DMA scale with it.
* While the mod is on every wrestler's object gets its scale (Dink's or 1:1:
* the objects of a player number go from one wrestler to the next).

 SUBR	dink_sx

	move	@dink_enabled,a14
	jrz	#done2
	PUSH	a0
	move	*a13(PLYRNUM),a14
	callr	dink_small_p
	jrz	#full2
	movi	DINK_MUL,a14
	mpys	a14,a1
	sra	8,a1
	movi	[DINK_SCALE,DINK_SCALE],a0
	move	a0,*a8(OSCALE),L
	PULL	a0
	rets
#full2	movi	[100h,100h],a0
	move	a0,*a8(OSCALE),L
	PULL	a0
#done2	rets


******************************************************************************
* a13 = the wrestler, a1 (a10) = an offset of his image: smaller for Dink.

 SUBR	dink_s1

	move	@dink_enabled,a14
	jrz	#done3
	move	*a13(PLYRNUM),a14
	callr	dink_small_p
	jrz	#done3
	movi	DINK_MUL,a14
	mpys	a14,a1
	sra	8,a1
#done3	rets

 SUBR	dink_s10

	PUSH	a1
	move	a10,a1
	callr	dink_s1
	move	a1,a10
	PULL	a1
	rets

	.end
