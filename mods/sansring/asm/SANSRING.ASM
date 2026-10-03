**************************************************************
*
* Sans ring: the switch the mod sets (mods/sansring/gen.txt)
*
**************************************************************

	.file	"sansring.asm"
	.title	"sans ring"
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
	.include	"ring.equ"

	.ref	leave_zero

	;Non-zero while the mod is switched on. The machine sets it every frame.
	BSSX	sans_on,16

	.text
	.even

******************************************************************************
* A wrestler that reaches the edge of the ring is outside it: walking, running, or thrown, pushed or kicked
* there (the game calls ck_climb_out_top / _bot / _side when its position went past the edge, and then puts it
* back at the rope; gen.txt makes it call this instead and go on without stopping him).
* The wrestler is only marked as outside; what carries him out is his own movement.
*
* >a13 = *Plyr process
* >a0 = 0 the top edge, 1 the bottom edge
* Trashes a14

 SUBR	sans_exit

	move	*a13(PLYRMODE),a14
	cmpi	MODE_ATTACHED,a14		;held or holding: the pair stays in the ring
	jreq	#no
	cmpi	MODE_OPPOVERHEAD,a14
	jreq	#no
	move	*a13(CLIMBING_THRU),a14		;coming in through the ropes
	jrnz	#no
	;Only when he is on his way out: the stick points out, or he is moving out (thrown, pushed). Coming in from the
	;floor he stands right at the edge, and without this he was sent out again at once.
	move	a0,a0
	jrnz	#bottom
	move	*a13(STICK_VAL_CUR),a14
	btst	MOVE_UP_BIT,a14
	jrnz	#out
	move	*a13(OBJ_ZVEL),a14,L
	jrn	#out
	rets
#bottom	move	*a13(STICK_VAL_CUR),a14
	btst	MOVE_DOWN_BIT,a14
	jrnz	#out
	move	*a13(OBJ_ZVEL),a14,L
	jrp	#out
	rets
#out	movk	1,a14
	move	a14,*a13(INRING)
#no	rets
