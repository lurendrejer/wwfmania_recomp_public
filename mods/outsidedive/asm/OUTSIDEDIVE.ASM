**************************************************************
*
* Dives to the outside (mods/outsidedive/gen.txt)
*
* A running attack started in the ring at an opponent outside it (the wrestler's target, CLOSEST_NUM) goes over
* the ropes and comes down on him: the attack against a standing opponent (clotheslines, flying kicks, ...) and
* the one against a lying one (belly flops, elbows, butt drops, stomps), every wrestler's own.
*
**************************************************************

	.file	"outsidedive.asm"
	.title	"dives to the outside"
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
	.include	"anim.equ"

	.ref	process_ptrs

DV_SLOTS	.equ	16		;wrestlers by PLYRNUM
DV_MAXDX	.equ	480		;how far away (X, Z) the opponent outside may be
DV_MAXDZ	.equ	220
DV_AIM_UP	.equ	MAT_Y		;the height at which he reaches a standing opponent: the mat's, where the original flight ends
DV_MAXXV	.equ	>140000		;the fastest he flies in X and in Z (pixels per tick, 16.16)
DV_MAXZV	.equ	>a0000
DV_DOWNYV	.equ	>70000		;at least this upward speed for a dive on a lying opponent: over the top rope
DV_MAXT		.equ	90		;ticks before a dive that never landed is given up
DV_LIFT		.equ	20		;ticks the attack may take to leave the mat

	;Non-zero while the mod is switched on. The machine sets it every frame.
	BSSX	dive_on,16

	;Per wrestler (PLYRNUM): 0 = no dive, 1 = the button handler started something (still on the mat), 2 = in the air
	BSSX	dive_st,16*DV_SLOTS
	BSSX	dive_tk,16*DV_SLOTS		;ticks since the attack started
	BSSX	dive_aim,16*DV_SLOTS		;the height (Y) at which he must be over the opponent
	BSSX	dive_low,16*DV_SLOTS		;non-zero: the opponent lies down
	BSSX	dive_md,16*DV_SLOTS		;his PLYRMODE when he left the mat (INAIR for most, NORMAL for Lex Luger)
	BSSX	dive_tgt,32*DV_SLOTS		;the opponent's process
	BSSX	dive_hb,16*DV_SLOTS		;his attack's hit box on this dive: 0 not on yet, 1 was on, 2 turned on again by dive_tick,
						;3 it hit (dive_hit)

	;Set by dive_pre while the button handler runs: the target is outside and ahead of him
	BSSX	dive_pend,16
	BSSX	dive_pt,32			;the target of that press
	BSSX	dive_sx,16			;CLOSEST_XDIST and CLOSEST_ZDIST as they were
	BSSX	dive_sz,16
	BSSX	dive_ab,32			;his animation (ANIBASE) before the handler

	;the word of TABLE for the wrestler whose word offset is in a1 (SLOTW) or a7 (SLOTW7) := a14; SLOTR reads it into REG
SLOTW	.macro	TABLE
	PUSH	a1
	addi	:TABLE:,a1
	move	a14,*a1
	PULL	a1
	.endm
SLOTW7	.macro	TABLE
	PUSH	a7
	addi	:TABLE:,a7
	move	a14,*a7
	PULL	a7
	.endm
SLOTR	.macro	TABLE,REG
	move	a7,:REG:
	addi	:TABLE:,:REG:
	move	*:REG:,:REG:
	.endm

	.text
	.even

******************************************************************************
* Before a wrestler's mode_running calls its button handler (gen.txt, every wrestler). When the target is outside
* the ring, ahead of him and near enough, the handler is made to see him close (CLOSEST_XDIST/ZDIST 0), so the
* tables of the running attacks pick the attack for a near opponent, and dive_xmid lets Bam Bam's and Doink's
* clothesline start anywhere.
*
* >a13 = *Plyr process
* Keeps every register but a14.

 SUBR	dive_pre

	clr	a14
	move	a14,@dive_pend
	move	@dive_on,a14
	jrz	#px
	PUSH	a1,a2,a3
	move	*a13(INRING),a14
	jrnz	#pno			;he must be in the ring
	move	*a13(CLOSEST_NUM),a1
	X32	a1
	addi	process_ptrs,a1
	move	*a1,a1,L
	jrz	#pno
	move	*a1(INRING),a14
	jrz	#pno			;and the opponent outside
	move	*a1(OBJ_ZPOSINT),a2
	move	*a13(OBJ_ZPOSINT),a3
	sub	a3,a2
	abs	a2
	cmpi	DV_MAXDZ,a2
	jrgt	#pno
	move	*a1(OBJ_XPOSINT),a2
	move	*a13(OBJ_XPOSINT),a3
	sub	a3,a2			;a2 = his X - mine
	move	*a13(MOVE_DIR),a14
	btst	PLAYER_RIGHT_BIT,a14
	jrnz	#prt
	neg	a2			;running left: he must be on the left
#prt	move	a2,a2
	jrle	#pno			;behind me
	cmpi	DV_MAXDX,a2
	jrgt	#pno
	move	a1,@dive_pt,L
	movk	1,a14
	move	a14,@dive_pend
	move	*a13(ANIBASE),a14,L
	move	a14,@dive_ab,L
	move	*a13(CLOSEST_XDIST),a14
	move	a14,@dive_sx
	move	*a13(CLOSEST_ZDIST),a14
	move	a14,@dive_sz
	move	*a1(PLYRMODE),a14	;a lying opponent: the tables' rows for him want him close (176 or so)
	cmpi	MODE_ONGROUND,a14
	jreq	#pnear
	cmpi	MODE_DEAD,a14
	jrne	#pno			;a standing one: the distances stay (Undertaker's clothesline is a head butt under 70)
#pnear	clr	a14
	move	a14,*a13(CLOSEST_XDIST)
	move	a14,*a13(CLOSEST_ZDIST)
#pno	PULL	a1,a2,a3
#px	rets

******************************************************************************
* After the button handler of mode_running (gen.txt). If dive_pre saw a target outside, the distances go back, and
* when the handler started something (he is no longer running) a dive begins: dive_tick follows it off the mat.
*
* >a13 = *Plyr process
* Keeps every register but a14.

 SUBR	dive_post

	move	@dive_pend,a14
	jrz	#qx
	PUSH	a1,a2
	clr	a14
	move	a14,@dive_pend
	move	@dive_sx,a14
	move	a14,*a13(CLOSEST_XDIST)
	move	@dive_sz,a14
	move	a14,*a13(CLOSEST_ZDIST)
	move	*a13(ANIBASE),a14,L	;a new animation (some set the mode a few ticks later), or not running any more
	move	@dive_ab,a1,L
	cmp	a1,a14
	jrne	#qgo
	move	*a13(PLYRMODE),a14
	cmpi	MODE_RUNNING,a14
	jreq	#qno			;still running: the handler did nothing
#qgo
	move	*a13(PLYRNUM),a1
	cmpi	DV_SLOTS,a1
	jrge	#qno
	move	@dive_pt,a2,L
	move	a1,a14
	sll	5,a14
	addi	dive_tgt,a14
	move	a2,*a14,L
	sll	4,a1			;a1 = word offset of this wrestler
	movk	1,a14
	SLOTW	dive_st
	clr	a14
	SLOTW	dive_tk
	SLOTW	dive_hb
	clr	a14			;lying: down to the floor where he lies
	move	*a2(PLYRMODE),a2
	cmpi	MODE_ONGROUND,a2
	jreq	#qlow
	cmpi	MODE_DEAD,a2
	jreq	#qlow
	SLOTW	dive_low
	movi	DV_AIM_UP,a14
	jruc	#qaim
#qlow	SLOTW	dive_aim
	movk	1,a14
	SLOTW	dive_low
	jruc	#qno
#qaim	SLOTW	dive_aim
#qno	PULL	a1,a2
#qx	rets

******************************************************************************
* In Bam Bam's and Doink's flying clothesline (gen.txt): the original only starts it near the middle of the ring.
* For a dive the place does not matter.
*
* >a0 = his X, a13 = *Plyr process
* <a0 = RING_X_MID for a dive, else as it was
* Trashes a14

 SUBR	dive_xmid

	move	@dive_pend,a14
	jrz	#mx
	movi	RING_X_MID,a0
#mx	rets

******************************************************************************
* At the start of confine_wrestler (gen.txt), every wrestler, twice a tick. While a dive is in the air the ropes do
* not hold him, and his speed in X and Z is set so that he is over the opponent when he comes down to the height of
* dive_aim: the mat's height for a standing opponent (the original flight's length, so the attack's own hit box is
* still on), the floor for a lying one (the attack hits on landing).
*
* >a13 = *Plyr process
* <a0 = non-zero: leave him unconfined
* Keeps every register but a0 and a14.

 SUBR	dive_tick

	clr	a0
	move	@dive_on,a14
	jrz	#tx
	move	*a13(PLYRNUM),a14
	cmpi	DV_SLOTS,a14
	jrge	#tx
	PUSH	a1,a2,a3,a4,a5,a6,a7
	move	a14,a7
	sll	4,a7			;a7 = word offset of this wrestler
	SLOTR	dive_st,a1
	jrz	#tout
	SLOTR	dive_tk,a14
	inc	a14
	SLOTW7	dive_tk
	cmpi	1,a1
	jrne	#tair
	cmpi	DV_LIFT,a14		;on the mat: an attack that does not leave it (a slap, a head butt) is no dive
	jrgt	#tend
	jruc	#tgo
#tair	cmpi	DV_MAXT,a14
	jrgt	#tend
	SLOTR	dive_md,a2		;in the air, a new mode (hit, or down on the floor): over
	move	*a13(PLYRMODE),a14
	cmp	a2,a14
	jrne	#tend
#tgo
	move	*a13(GROUND_Y),a2
	sll	16,a2
	move	*a13(OBJ_YPOS),a3,L
	cmpi	1,a1
	jrne	#tflying
	cmp	a2,a3			;still on the mat: confined as always
	jrle	#tout
	movk	2,a14
	SLOTW7	dive_st			;off the mat
	move	*a13(PLYRMODE),a14
	SLOTW7	dive_md
	SLOTR	dive_low,a14
	jrz	#tflying
	move	*a13(OBJ_YVEL),a14,L
	cmpi	DV_DOWNYV,a14
	jrge	#tflying
	movi	DV_DOWNYV,a14
	move	a14,*a13(OBJ_YVEL),L
#tflying
	cmp	a2,a3			;back down on the ground: over
	jrgt	#tup
	move	*a13(OBJ_YVEL),a14,L
	jrle	#tend
#tup
	;The attack's hit box is on for the original flight only, which ends at the ropes: until it has hit, it stays on
	;while he is in the air (dive_hit marks a hit; MODE_STATUS is no sign, some animations set it on a miss)
	move	*a13(ANIMODE),a1
	btst	MODE_CHECKHIT_BIT,a1
	jrz	#thoff
	SLOTR	dive_hb,a14
	jrnz	#thdone
	movk	1,a14
	SLOTW7	dive_hb
	jruc	#thdone
#thoff	SLOTR	dive_hb,a14
	jrz	#thdone			;no attack yet
	cmpi	3,a14
	jreq	#thdone			;it hit
	move	*a13(ATTACK_MODE),a14	;a grab is only on in its own window: the victim of a late one would wait for an animation
	movi	#tgrabs,a2		;that has gone on to its miss (and hang as a puppet)
#tgrl	move	*a2+,a3
	jrn	#tgrok
	cmp	a3,a14
	jreq	#thdone
	jruc	#tgrl
#tgrok	ori	MODE_CHECKHIT,a1
	move	a1,*a13(ANIMODE)
	movk	2,a14
	SLOTW7	dive_hb
#thdone
	;ticks until he is down at dive_aim again: the flight played forward with his gravity
	move	*a13(OBJ_YVEL),a4,L
	move	*a13(OBJ_GRAVITY),a5,L
	jrnz	#tgok
	movi	GRAVITY,a5
#tgok	SLOTR	dive_aim,a6
	sll	16,a6
	cmp	a6,a3
	jrle	#tfree			;already below it: fly on as he is
	clr	a1
#tsim	add	a4,a3
	sub	a5,a4
	inc	a1
	cmpi	DV_MAXT,a1
	jrgt	#tfree
	cmp	a6,a3
	jrgt	#tsim
	;a1 = ticks; X and Z speed to be over the opponent then
	move	*a13(PLYRNUM),a14
	sll	5,a14
	addi	dive_tgt,a14
	move	*a14,a2,L
	move	*a2(OBJ_XPOSINT),a3
	move	*a13(OBJ_XPOSINT),a14
	sub	a14,a3
	sll	16,a3
	divs	a1,a3
	movi	DV_MAXXV,a14
	cmp	a14,a3
	jrle	#tx1
	move	a14,a3
#tx1	neg	a14
	cmp	a14,a3
	jrge	#tx2
	move	a14,a3
#tx2	move	a3,*a13(OBJ_XVEL),L
	move	*a2(OBJ_ZPOSINT),a3
	move	*a13(OBJ_ZPOSINT),a14
	sub	a14,a3
	sll	16,a3
	divs	a1,a3
	movi	DV_MAXZV,a14
	cmp	a14,a3
	jrle	#tz1
	move	a14,a3
#tz1	neg	a14
	cmp	a14,a3
	jrge	#tz2
	move	a14,a3
#tz2	move	a3,*a13(OBJ_ZVEL),L
#tfree	movk	1,a0
	jruc	#tout
#tgrabs	.word	AMODE_GRABTHROW,AMODE_GRABHOLD,AMODE_GRABFLING,AMODE_HEADHOLD,AMODE_HEADKNEES,AMODE_PUPPET
	.word	AMODE_PUPPET2,AMODE_PUPPET_HDGRAB,AMODE_TOMB,AMODE_PUPPET_NOFLAIL,AMODE_PUPPET_TOSS,-1
	.even
#tend	clr	a14
	SLOTW7	dive_st
	SLOTR	dive_hb,a14		;a hit box this dive turned on, and nothing hit: off
	cmpi	2,a14
	jrne	#tout
	clr	a14
	SLOTW7	dive_hb
	move	*a13(ANIMODE),a14
	andni	MODE_CHECKHIT,a14
	move	a14,*a13(ANIMODE)
#tout	PULL	a1,a2,a3,a4,a5,a6,a7
#tx	rets

******************************************************************************
* In the collision code where an attack has hit (COLLIS.ASM, gen.txt): marks it for dive_tick, which then leaves the
* hit box alone.
*
* >a10 = *the attacker's process
* Trashes a14

 SUBR	dive_hit

	move	@dive_on,a14
	jrz	#hx
	move	*a10(PLYRNUM),a14
	cmpi	DV_SLOTS,a14
	jrge	#hx
	sll	4,a14
	addi	dive_hb,a14
	PUSH	a0
	move	*a14,a0
	jrz	#hn			;no dive (and no hit box of one)
	movk	3,a0
	move	a0,*a14
#hn	PULL	a0
#hx	rets

	.end
