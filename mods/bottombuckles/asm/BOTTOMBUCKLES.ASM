**************************************************************
*
* Bottom turnbuckles (mods/bottombuckles/gen.txt)
*
**************************************************************

	.file	"bottombuckles.asm"
	.title	"bottom turnbuckles"
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

	.ref	get_rope_x

BB_XL	.equ	816			;where he stands on a near post, x: the game's rope line is well inside the post it draws
BB_XR	.equ	1336
BB_Y	.equ	135			;his height (the game's for the far posts, 155, floats above the near post)

FC_XL	.equ	ARENA_TOP_LEFT		;the crowd fence's back corners: where the back rail meets the side rails (RING.EQU)
FC_XR	.equ	ARENA_TOP_RIGHT
FC_Z	.equ	ARENA_TOP
FC_ZMAX	.equ	800
FB_XL	.equ	ARENA_BOT_LEFT		;the front corners (RING.EQU); Down + Left/Right climbs them, with his back to the screen
FB_XR	.equ	ARENA_BOT_RIGHT
FB_Z	.equ	ARENA_BOT
FB_OUT	.equ	55			;the corner is drawn this far outside the fence's line (right, left)
FB_OUTL	.equ	45
FB_Y	.equ	45
FB_ZMIN	.equ	1600			;in front of the ring, above this Z: a front fence corner			;behind the ring, above this Z: the fence corner, not a turnbuckle
FC_LEEZ	.equ	40			;how near the back rail and the corner he must be, in Z and X
FC_LEEX	.equ	60
FC_RX1	.equ	860			;well inside the ring's ropes: a wrestler here is in the ring
FC_RX2	.equ	1290
FC_RZ1	.equ	1050
FC_RZ2	.equ	1320
FC_Y	.equ	62			;his height on the fence corner (the climb animations are drawn for the ring's posts, 155)

	;Non-zero while the mod is switched on. The machine sets it every frame.
	BSSX	bb_on,16

	;Ticks since each wrestler (by PLYRNUM) landed on a near post, for the stand animation (bb_swap_ud).
	BSSX	bb_timer,4*16

	;Non-zero: a leap from the fence only flies at an opponent who is outside the ring too (the mod's number 2); else he hops in place.
	BSSX	fc_only_out,16

	;A copy of a LEAPATOPP's numbers (ANIM.ASM _ani_leapatopp) for a leap from a fence corner: ticks, most in all, in X, in Z,
	;highest Y velocity (long), then the rest as they were.
	BSSX	fc_leapbuf,160

	.text
	.even

******************************************************************************
* Is the wrestler at a back corner of the crowd fence, pushing at it (Up + Left/Right)? Tried first in
* climb_turnbuckle (gen.txt): the game's own top test would take him for the far turnbuckle from anywhere behind
* the ring on the right side.
*
* >a13 = *Plyr process
* <a0 = 0 if not, else a3 = MOVE_UP_LEFT / MOVE_UP_RIGHT
* Trashes a1, a3, a4, a14

 SUBR	fc_check

	move	*a13(STICK_VAL_CUR),a4
	move	*a13(INRING),a0
	jrz	#fno			;INRING: 0 = in the ring, 1 = outside: the fence is out there
	move	*a13(OBJ_ZPOSINT),a0
	cmpi	FB_ZMIN,a0
	jrge	#front
	cmpi	FC_Z+FC_LEEZ,a0
	jrgt	#fno
	btst	MOVE_UP_BIT,a4
	jrz	#fno

	move	*a13(OBJ_XPOSINT),a0
	cmpi	RING_X_CENTER,a0
	jrgt	#fright
	cmpi	FC_XL+FC_LEEX,a0
	jrgt	#fno
	movk	MOVE_UP_LEFT,a3
	cmp	a3,a4
	jrne	#fno
	movk	1,a0
	rets

#fright	cmpi	FC_XR-FC_LEEX,a0
	jrlt	#fno
	movk	MOVE_UP_RIGHT,a3
	cmp	a3,a4
	jrne	#fno
	movk	1,a0
	rets

#front	cmpi	FB_Z-FC_LEEZ,a0
	jrlt	#fno
	btst	MOVE_DOWN_BIT,a4
	jrz	#fno
	move	*a13(OBJ_XPOSINT),a0
	cmpi	RING_X_CENTER,a0
	jrgt	#fbright
	cmpi	FB_XL+FC_LEEX,a0
	jrgt	#fno
	movk	MOVE_DOWN_LEFT,a3
	cmp	a3,a4
	jrne	#fno
	callr	bb_reset_timer		;the count to the stand pose (bb_swap_ud); keeps a3
	movk	1,a0
	rets

#fbright	cmpi	FB_XR-FC_LEEX,a0
	jrlt	#fno
	movk	MOVE_DOWN_RIGHT,a3
	cmp	a3,a4
	jrne	#fno
	callr	bb_reset_timer
	movk	1,a0
	rets

#fno	clr	a0
	rets

* The place the leap up aims at for the wrestler's side (tgt_tbukl in SHNSEQ2.ASM, gen.txt): X and Z words, Z
* 16 more since tgt_tbukl takes 16 off.
* <a0 = * the two words
* Trashes a14

 SUBR	fc_table

	move	*a13(OBJ_ZPOSINT),a14
	cmpi	FB_ZMIN,a14
	jrge	#ffront
	move	*a13(OBJ_XPOSINT),a14
	cmpi	RING_X_CENTER,a14
	jrgt	#fr
	movi	#ftl,a0
	rets
#fr	movi	#ftr,a0
	rets
#ffront	move	*a13(OBJ_XPOSINT),a14
	cmpi	RING_X_CENTER,a14
	jrgt	#fbr
	movi	#fbl,a0
	rets
#fbr	movi	#fbrt,a0
	rets
#ftl	.word	FC_XL-24,FC_Z+16		;the drawn corner is a little further out than the fence's line
#ftr	.word	FC_XR+24,FC_Z+16
#fbl	.word	FB_XL-FB_OUTL,FB_Z-16
#fbrt	.word	FB_XR+FB_OUT,FB_Z-16

******************************************************************************
* With the mod's number 2: is the closest opponent in the ring, so that a leap from the fence must not fly at him?
*
* >a13 = *Plyr process
* <a0 = non-zero if the leap is to stay on the spot
* Trashes a14

 SUBR	fc_stay

	clr	a0
	move	@fc_only_out,a14
	jrz	#fqx
	move	*a13(CLOSEST_NUM),a0
	X32	a0
	addi	process_ptrs,a0
	move	*a0,a0,L
	jrz	#fqn			;nobody
	move	*a0(INRING),a0
	jrnz	#fqn			;outside as well: fly
	movk	1,a0			;in the ring: stay
	rets
#fqn	clr	a0
#fqx	rets

******************************************************************************
* The leap from a fence corner (called from _ani_leapatpos in ANIM.ASM, gen.txt, when the wrestler is outside the ring):
* the animation's flight (24 ticks, 200 pixels) is made for the ring's posts. From the fence the opponent is 500 to 900
* pixels away, so the flight gets as many ticks as the distance needs (24 to 48; a longer flight is a higher arc), and
* the animation's limit on the distance is lifted to match.
*
* >a13 = *Plyr process (TGT_XOFF / TGT_ZOFF: the target)
* <a8 = ticks, a9 = the most it may travel
* Trashes a0, a1, a2

 SUBR	fc_leap

	move	*a13(TGT_XOFF),a1
	move	*a13(OBJ_XPOSINT),a2
	sub	a2,a1
	abs	a1
	move	*a13(TGT_ZOFF),a0
	move	*a13(OBJ_ZPOSINT),a2
	sub	a2,a0
	abs	a0
	add	a0,a1			;x + z: more than the real distance, which is fine
	move	a1,a9
	addi	100,a9
	callr	fc_stay
	move	a0,a0
	jrz	#fl_go
	clr	a9			;in place: no distance
#fl_go	movk	14,a2
	divu	a2,a1			;a1 = ticks
	movk	24,a8
	cmp	a8,a1
	jrle	#fl_done
	move	a1,a8
	cmpi	48,a8
	jrle	#fl_done
	movk	24,a8
	sll	1,a8
#fl_done
	rets

* The same for the animations that leap at the opponent's predicted place (_ani_leapatopp, LEAPATOPP): a copy of their
* numbers is made and the ticks and the limits are set for the distance to the closest opponent, the anim pointer a4 is
* put on the copy.
*
* >a4 = * the numbers, a13 = *Plyr process
* <a4 = * the copy
* Trashes a0, a1, a2, a3, a14

 SUBR	fc_leapopp

	movi	fc_leapbuf,a1
	move	a4,a2
	movk	5,a0
#flc	move	*a2+,*a1+,L
	dsj	a0,#flc
	move	*a13(CLOSEST_NUM),a2
	X32	a2
	addi	process_ptrs,a2
	move	*a2,a2,L
	jrz	#flx			;nobody: leave the numbers alone
	move	*a2(OBJ_XPOSINT),a0
	move	*a13(OBJ_XPOSINT),a1
	sub	a1,a0
	abs	a0			;a0 = distance in X
	move	*a2(OBJ_ZPOSINT),a1
	move	*a13(OBJ_ZPOSINT),a3
	sub	a3,a1
	abs	a1			;a1 = distance in Z
	movi	fc_leapbuf,a4
	move	a0,a3
	add	a1,a3			;a3 = X + Z
	addi	100,a0
	move	a0,*a4(32),W		;most in X
	addi	100,a1
	move	a1,*a4(48),W		;most in Z
	addi	100,a3
	move	a3,*a4(16),W		;most in all
	subi	100,a3
	callr	fc_stay
	move	a0,a0
	jrz	#fl2
	clr	a0
	move	a0,*a4(16),W
	move	a0,*a4(32),W
	move	a0,*a4(48),W
#fl2	movk	14,a2
	divu	a2,a3			;a3 = ticks the distance needs
	move	*a4,a2,W		;the animation's own ticks
	cmp	a2,a3
	jrle	#fly
	move	a3,a2
	cmpi	48,a2
	jrle	#fly
	movk	24,a2
	sll	1,a2
#fly	move	a2,*a4,W
	movi	>240000,a2
	move	a2,*a4(64),L		;room for a high arc
	rets
#flx	movi	fc_leapbuf,a4
	rets

******************************************************************************
* Called every tick from keep_onscreen (gen.txt) for every wrestler: a wrestler who left a fence corner in a leap and is
* over the ring now is in the ring (INRING 0, the mat is the ground). The game's own leaps from the ring's posts never
* leave INRING at 1 for a wrestler in the middle of the ring; it would push him out.
*
* Trashes a14

 SUBR	fc_enter

	move	@bb_on,a14
	jrz	#fex
	move	*a13(INRING),a14
	jrz	#fex
	PUSH	a0
	move	*a13(OBJ_ZPOSINT),a0
	cmpi	FC_RZ1,a0
	jrlt	#fxp
	cmpi	FC_RZ2,a0
	jrgt	#fxp
	move	*a13(OBJ_XPOSINT),a0
	cmpi	FC_RX1,a0
	jrlt	#fxp
	cmpi	FC_RX2,a0
	jrgt	#fxp
	clr	a14
	move	a14,*a13(INRING)
	movi	MAT_Y,a14
	move	a14,*a13(GROUND_Y)
#fxp	PULL	a0
#fex	rets

******************************************************************************
* Is the wrestler at a bottom turnbuckle, pushing at it? The same tests as the top ones in climb_turnbuckle
* (WRESTLE2.ASM), for the bottom edge and DOWN + LEFT/RIGHT.
*
* >a13 = *Plyr process
* <a0 = 0 if not, else a3 = the direction to face (MOVE_DOWN_LEFT or MOVE_DOWN_RIGHT)
* Trashes a1, a3, a4, a14 (the game's caller is on its way out)

 SUBR	bb_check

	move	*a13(STICK_VAL_CUR),a4
	btst	MOVE_DOWN_BIT,a4
	jrz	#no
	move	*a13(INRING),a0
	jrnz	#no
	move	*a13(OBJ_ZPOSINT),a0
	cmpi	RING_BOT-5,a0		;5 pixels of leeway, as at the top
	jrlt	#no

	move	*a13(OBJ_XPOSINT),a0
	cmpi	RING_X_CENTER,a0
	jrgt	#right
	calla	get_rope_x
	move	*a13(OBJ_COLLX1),a1
	subk	5,a1
	cmp	a0,a1
	jrgt	#no
	movk	MOVE_DOWN_LEFT,a3
	cmp	a3,a4
	jrne	#no
	callr	bb_reset_timer
	movk	1,a0
	rets

#right	calla	get_rope_x
	move	*a13(OBJ_COLLX2),a1
	addk	5,a1
	cmp	a0,a1
	jrlt	#no
	movk	MOVE_DOWN_RIGHT,a3
	cmp	a3,a4
	jrne	#no
	callr	bb_reset_timer
	movk	1,a0
	rets

#no	clr	a0
	rets

* Starts the count of ticks since landing for this wrestler. Keeps a3.
 SUBRP	bb_reset_timer

	move	*a13(PLYRNUM),a14
	cmpi	4,a14
	jrge	#rx
	sll	4,a14
	addi	bb_timer,a14
	PUSH	a2
	clr	a2
	move	a2,*a14
	PULL	a2
#rx	rets

******************************************************************************
* Called every tick from every wrestler's mode_onturnbkl (gen.txt), before it looks at the stick, i.e. while
* a wrestler stands on a turnbuckle (or has just leapt up onto it).
*
* 1. The stick as seen by a wrestler on a near (bottom) post: Up and Down exchanged, so that Up climbs down there
*    (the game's Down is what climbs up, and away from the ring).
* 2. On a near post he stands with his back to the screen: some ticks after landing, his own "stand" animation
*    (stand8, the one for standing facing away) replaces the game's on-a-turnbuckle animation (which shows the front,
*    drawn for the far posts); the mode bits that keep him up there are kept every tick.
*
* >a0 = the stick value (STICK_VAL_CUR), a13 = *Plyr process
* <a0 = the stick value to use
* Trashes a14

 SUBRP	fc_stand

	PUSH	a1,a2
	move	*a13(OBJ_XPOSINT),a1	;on the corner's line, X
	cmpi	RING_X_CENTER,a1
	jrgt	#fsr
	movi	FC_XL-8,a1
	jruc	#fsx
#fsr	movi	FC_XR+8,a1
#fsx	move	a1,*a13(OBJ_XPOSINT)
	move	*a13(OBJ_YPOSINT),a1	;down to the fence's height, a few pixels a tick
	cmpi	FC_Y,a1
	jrle	#fsd
	subk	6,a1
	cmpi	FC_Y,a1
	jrge	#fsy
	movi	FC_Y,a1
#fsy	move	a1,*a13(OBJ_YPOSINT)
#fsd	PULL	a1,a2
	rets

 SUBR	bb_swap_ud

	move	@bb_on,a14
	jrz	#x
	move	*a13(OBJ_ZPOSINT),a14
	cmpi	FC_ZMAX,a14
	jrge	#nofc
	callr	fc_stand		;on a fence corner: the game's stick, his place set here
	jruc	#x
#nofc	cmpi	(RING_TOP+RING_BOT)/2,a14
	jrlt	#x			;a far post: as the game has it
	PUSH	a1,a2
	move	a0,a1
	andi	MOVE_UP+MOVE_DOWN,a1
	jrz	#y
	cmpi	MOVE_UP+MOVE_DOWN,a1
	jreq	#y			;both: nothing to exchange
	xori	MOVE_UP+MOVE_DOWN,a0	;one of them: the other
#y
	move	*a13(WRESTLERNUM),a1
	X32	a1
	addi	#stand8,a1
	move	*a1,a1,L		;this wrestler's stand8 animation
	move	*a13(ANIBASE),a2,L
	cmp	a1,a2
	jreq	#standing
	move	*a13(PLYRNUM),a2	;a timer: ticks since he landed
	cmpi	4,a2
	jrge	#done
	sll	4,a2
	addi	bb_timer,a2
	PUSH	a3
	move	*a2,a3
	inc	a3
	move	a3,*a2
	cmpi	14,a3			;the game's landing (shake of the corner, offsets) takes about this long
	PULL	a3
	jrlt	#done
	PUSH	a0
	move	a1,a0
	calla	change_anim1a
	PULL	a0
	move	*a13(WRESTLERNUM),a14	;nose to the other side, for the ones whose stand pose is the mirror image of how the game left them
	sll	4,a14
	addi	#flip,a14
	move	*a14,a14
	move	*a13(OBJ_CONTROL),a2
	xor	a14,a2
	move	a2,*a13(OBJ_CONTROL)
#standing
	move	*a13(OBJ_ZPOSINT),a2
	cmpi	FB_ZMIN,a2
	jrlt	#ringpost
	move	*a13(OBJ_XPOSINT),a2	;a front fence corner: his place, every tick
	cmpi	RING_X_CENTER,a2
	jrgt	#fbr2
	movi	FB_XL-FB_OUTL,a2
	jruc	#fbx2
#fbr2	movi	FB_XR+FB_OUT,a2
#fbx2	move	a2,*a13(OBJ_XPOSINT)
	movi	FB_Y,a2
	move	a2,*a13(OBJ_YPOSINT)
	jruc	#keepup
#ringpost
	move	*a13(OBJ_XPOSINT),a2	;every tick, since the game's rope line pulls him back
	cmpi	RING_X_CENTER,a2
	jrgt	#rt2
	movi	BB_XL,a2
	jruc	#st2
#rt2	movi	BB_XR,a2
#st2	move	a2,*a13(OBJ_XPOSINT)
	movi	BB_Y,a2
	move	a2,*a13(OBJ_YPOSINT)
#keepup	move	*a13(ANIMODE),a2	;change_anim1a and stand8 set the normal mode: keep him up there
	ori	MODE_NOGRAVITY|MODE_NOAUTOFLIP|MODE_NOCONFINE|MODE_UNINT|MODE_OVERLAP,a2
	move	a2,*a13(ANIMODE)
#done	PULL	a1,a2
#x	rets

	.ref	hrt_stand8_anim,rzr_stand8_anim,und_stand8_anim,yok_stand8_anim,shn_stand8_anim
	.ref	bam_stand8_anim,dnk_stand8_anim,lex_stand8_anim

#stand8
	.long	hrt_stand8_anim		;0 Bret Hart
	.long	rzr_stand8_anim		;1 Razor Ramon
	.long	und_stand8_anim		;2 Undertaker
	.long	yok_stand8_anim		;3 Yokozuna
	.long	shn_stand8_anim		;4 Shawn Michaels
	.long	bam_stand8_anim		;5 Bam Bam
	.long	dnk_stand8_anim		;6 Doink
	.long	dnk_stand8_anim		;7 Adam Bomb
	.long	lex_stand8_anim		;8 Lex Luger
	.long	dnk_stand8_anim		;9 Referee

#flip
	.word	0			;0 Bret Hart
	.word	0			;1 Razor Ramon
	.word	0			;2 Undertaker
	.word	M_FLIPH			;3 Yokozuna
	.word	0			;4 Shawn Michaels
	.word	0			;5 Bam Bam
	.word	M_FLIPH			;6 Doink
	.word	M_FLIPH			;7 Adam Bomb (Doink's animation)
	.word	0			;8 Lex Luger
	.word	M_FLIPH			;9 Referee (Doink's animation)
