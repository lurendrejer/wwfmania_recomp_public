**************************************************************
*
* The chair (mods/chair/gen.txt, mods/chair/README.md)
*
**************************************************************

	.file	"chair.asm"
	.title	"chair mod"
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
	.include	"damage.equ"
	.include	"ring.equ"

	.ref	change_anim1a,ADD_VOICE,CHAIR_P,calc_line_x
	.ref	CHSWNG01,CHSWNG02,CHSWNG03,CHSWNG04,CHSWNG05,CHSWNG06,CHSWNG07
	.ref	CHSWNG08,CHSWNG09,CHSWNFL

	;Non-zero: Down, Down, Block picks up a chair, Punch swings it. The machine sets it
	;every frame while the mod is on.
	BSSX	chair_enabled,16
	;Non-zero: he keeps the chair after a swing and can swing it again; 0: he lets go of
	;it. Set by the machine.
	BSSX	chair_keep,16
	;Which of the commentator's two chair lines is next.
	BSSX	chair_line,16
	;Per wrestler (PLYRNUM): non-zero while he walks around with the chair.
	BSSX	chair_held,16*16
	;Per wrestler (PLYRNUM): the upper part of his holding frame, drawn as his walking torso
	;(chair_torso): the piece count word, then a copy of the frame's image header.
#THDR	equ	110h
	BSSX	chair_torso_hdrs,16*#THDR

#HDR	equ	0A0h			;one image header, rounded up
	;The chair frames' image headers, copied with a palette (see chair_headers).
	BSSX	chair_hdrs,10*0A0h

	.text
	.even

******************************************************************************
* Picking it up: a secret move, same layout as the others (newest first, then
* the joystick states before it, then the longest time allowed and the code).
* It is in every wrestler's secret move table (gen.txt).

 SUBR	chair_rec
	.word	B_BLOCK,		J_ALL
	.word	J_DOWN,			J_REAL_LR
	.word	J_DOWN,			J_REAL_LR
	.word	8000h | 32		;max
	.long	chair_secret

 SUBR	chair_secret
	move	@chair_enabled,a0
	jrz	#out

	move	*a13(ANIMODE),a0
	btst	MODE_UNINT_BIT,a0
	jrnz	#out

	move	*a13(PLYRMODE),a0
	cmpi	MODE_NORMAL,a0
	jrne	#out

	callr	chair_ringside
	jrz	#out
	movi	chair_ups,a0
	callr	chair_anim
	jrz	#out

	callr	chair_headers
	calla	change_anim1a
#out	rets

;Z clear when he stands at the side of the ring (left or right, not the top or
;bottom), where the chairs are kept under it: where he could climb in from the
;side (ck_climb_in_side, WRESTLE2.ASM): outside the ring, between the posts and
;within 10 pixels of the mat's edge. Trashes a0, a1, a6, a14.
chair_ringside
	move	*a13(INRING),a0		;0 = in the ring, 1 = outside
	jrz	#rno
	move	*a13(OBJ_ZPOSINT),a0
	subi	RING_Z_CENTER,a0
	abs	a0
	cmpi	0D8h,a0
	jrgt	#rno
	move	*a13(OBJ_XPOSINT),a0
	cmpi	RING_X_CENTER,a0
	jrlt	#rleft
	movi	vln_right_matedge2,a6
	move	*a13(OBJ_COLLX1),a14
	jruc	#rline
#rleft	movi	vln_left_matedge2,a6
	move	*a13(OBJ_COLLX2),a14
#rline	calla	calc_line_x		;a0 = the edge's x where he stands, 0 if out of range
	move	a0,a0
	jrz	#rno
	sub	a14,a0
	abs	a0
	cmpi	10,a0
	jrgt	#rno
	movk	1,a0			;Z clear
	rets
#rno	clr	a0
	rets

;The end of the swing (ANI_CODE): MODE_STATUS set when he keeps the chair
;(ANI_IFSTATUS then lifts it overhead again instead of letting go).
chair_keep_status
	move	*a13(ANIMODE),a0
	andni	MODE_STATUS,a0
	move	@chair_keep,a14
	jrz	#knot
	ori	MODE_STATUS,a0
#knot	move	a0,*a13(ANIMODE)
	rets

;a0 = one of the tables below: a0 = this wrestler's animation, Z if he has none.
chair_anim
	move	*a13(WRESTLERNUM),a14
	cmpi	8,a14
	jrhi	#anone
	sll	5,a14
	add	a14,a0
	move	*a0,a0,L
	rets
#anone	clr	a0
	rets

chair_ups
	.long	hrt_chair_up_anim	;0 Bret Hart
	.long	rzr_chair_up_anim	;1 Razor Ramon
	.long	und_chair_up_anim	;2 Undertaker
	.long	yok_chair_up_anim	;3 Yokozuna
	.long	shn_chair_up_anim	;4 Shawn Michaels
	.long	bam_chair_up_anim	;5 Bam Bam
	.long	dnk_chair_up_anim	;6 Doink
	.long	0			;7 spare
	.long	lex_chair_up_anim	;8 Lex Luger

chair_swings
	.long	hrt_chair_swing_anim
	.long	rzr_chair_swing_anim
	.long	und_chair_swing_anim
	.long	yok_chair_swing_anim
	.long	shn_chair_swing_anim
	.long	bam_chair_swing_anim
	.long	dnk_chair_swing_anim
	.long	0
	.long	lex_chair_swing_anim

;The end of the pick-up (ANI_CODE): the chair stays attached, overhead, and
;chair_control looks after it.
chair_grab
	callr	chair_held_ptr
	jrz	#gout
	movk	1,a14
	move	a14,*a0
#gout	rets

;a0 = this wrestler's word in chair_held (Z if PLYRNUM is out of range).
chair_held_ptr
	move	*a13(PLYRNUM),a0
	cmpi	15,a0
	jrhi	#hnone
	sll	4,a0
	addi	chair_held,a0
	rets
#hnone	clr	a0
	rets

******************************************************************************
* Every tick, first in check_secret_moves (gen.txt), for a wrestler with the
* chair: any button swings it (C set: the secret moves are not looked at, so none
* of them takes the button too). Anything but standing and walking lets go of it:
* a hit (the game has cleared the attached image), running, any other move.
* Keeps every register.

 SUBR	chair_control
	move	@chair_enabled,a14
	jrz	#cno
	mmtm	sp,a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11
	callr	chair_held_ptr
	jrz	#cout
	move	a0,a1
	move	*a1,a14
	jrz	#cout

	move	*a13(ATTIMG_CUR_FRAME),a14,L
	jrz	#drop
	move	*a13(PLYRMODE),a14
	cmpi	MODE_NORMAL,a14
	jrne	#drop
	move	*a13(ANIMODE),a14
	btst	MODE_UNINT_BIT,a14
	jrnz	#drop

	move	*a13(BUT_VAL_DOWN),a14
	andi	PLAYER_PUNCH_VAL|PLAYER_BLOCK_VAL|PLAYER_SPUNCH_VAL|PLAYER_KICK_VAL|PLAYER_SKICK_VAL,a14
	jrz	#cout

	movi	chair_swings,a0
	callr	chair_anim
	jrz	#cout
	clr	a14
	move	a14,*a1			;the swing has it now
	calla	change_anim1a
	mmfm	sp,a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11
	setc
	rets

#drop
	clr	a14
	move	a14,*a1
	move	a14,*a13(ATTIMG_CUR_FRAME),L
#cout
	mmfm	sp,a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11
#cno
	clrc
	rets

******************************************************************************
* Carrying it. His legs walk as usual (but facing the screen, chair_legs); above them (set_images, ANIM.ASM, which
* draws the primary frame and then the torso frame on the legs' secondary point)
* the chair is held overhead (gen.txt hooks the two calls):
*  chair_body:  standing (a frame with no torso piece): the holding frame instead.
*  chair_torso: walking: the holding frame's upper part instead of the torso,
*               spliced on the legs the way the game splices a torso: its waist
*               (chair_joints: where the wrestler's walking legs join, on average,
*               measured from the feet) is put on the legs' secondary point, so it
*               moves with the hips, and it is cut a few rows below the waist.

#OVERLAP equ	3

;a0 = * the frame about to be drawn, a13 = * wrestler: a0 = what to draw. Keeps every other register.
 SUBR	chair_body
	PUSH	a1,a14
	callr	chair_hold_hdr
	jrz	#bout
	move	*a0(IANI2X),a14,L
	inc	a14
	jrnz	#bout			;it has a torso piece: chair_torso
	move	a1,a0
#bout	PULL	a1,a14
	rets

;a0 = * the torso frame, a9 = * the legs' frame, a10/a11 = the torso's offsets, a13 = * wrestler:
;a0, a10, a11 = what to draw and where. Keeps every other register.
 SUBR	chair_torso
	PUSH	a1,a2,a3,a4,a5,a6,a14
	callr	chair_hold_hdr
	jrz	#tout
	move	*a13(WRESTLERNUM),a14
	cmpi	8,a14
	jrhi	#tout
	sll	5,a14
	addi	chair_joints,a14
	move	*a14+,a5		;the waist, x: the feet less it
	move	*a14,a6			;and y: up from the feet
	;rows: down to the waist and a little more
	move	*a1(IANIOFFY),a4
	sub	a6,a4
	addk	#OVERLAP,a4
	jrle	#tout
	move	*a1(10h),a14		;its height
	cmp	a14,a4
	jrlt	#rows_ok
	move	a14,a4
#rows_ok
	move	*a13(PLYRNUM),a2
	cmpi	15,a2
	jrhi	#tout
	sll	4,a2			;x16
	move	a2,a3
	sll	4,a3			;x256
	add	a3,a2			;x272 = #THDR
	addi	chair_torso_hdrs,a2
	movk	1,a14
	move	a14,*a2+		;one piece
	move	a2,a0			;the header copy
	move	a1,a3
	movk	15,a14			;15 words: the frame header
#copy	move	*a3+,*a2+
	dsj	a14,#copy
	move	a4,*a0(10h)		;fewer rows
	;the waist on the legs' secondary point (set_images puts a torso's anchor there
	;with a10 = their IANIOFFX - IANI2X, a11 = OBJ_YPOSINT + IANIOFFY - IANI2Y)
	move	*a9(IANIOFFX),a10
	move	*a9(IANI2X),a14
	sub	a14,a10
	sub	a5,a10
	move	*a9(IANIOFFY),a11
	move	*a9(IANI2Y),a14
	sub	a14,a11
	sub	a6,a11
	move	*a13(OBJ_YPOSINT),a14
	add	a14,a11
#tout	PULL	a1,a2,a3,a4,a5,a6,a14
	rets

;a0 = his FACING_DIR, as change_walk_anim (WRESTLE.ASM) picks his legs' walk with it: a0 = the
;same, but facing the screen (down) instead of away from it (up) while he carries the chair, so
;his legs face the way the holding frame does. Keeps every other register.
 SUBR	chair_legs
	PUSH	a1
	callr	chair_hold_hdr		;(keeps a0)
	jrz	#lout
	btst	0,a0			;MOVE_UP
	jrz	#lout
	xori	MOVE_UP|MOVE_DOWN,a0
#lout	PULL	a1
	rets

;a1 = * the holding frame's image header when this wrestler carries the chair (Z clear), else Z set.
chair_hold_hdr
	move	@chair_enabled,a1
	jrz	#hno
	PUSH	a0
	callr	chair_held_ptr
	jrz	#hpop
	move	*a0,a1
	jrz	#hpop
	move	*a13(WRESTLERNUM),a1
	cmpi	8,a1
	jrhi	#hzero
	sll	5,a1
	addi	chair_holds,a1
	move	*a1,a1,L
	jrz	#hpop
	move	*a1,a1,L		;the table entry holds * the header
	PULL	a0
	move	a1,a1
	rets
#hzero	clr	a1
#hpop	PULL	a0
#hno	clr	a1
	rets

******************************************************************************
* The chair images are listed in MISC.LOD under a header layout for wrestler
* frames, which has no palette; the attached image code (ANIM.ASM) reads one at
* ICMAP. The headers are copied to RAM with CHAIR_P added there, and CHAIR_SWING
* points at the copies. Keeps every register.

chair_headers
	PUSH	a1,a2,a3,a4,a14
	movi	chair_src,a3
	movi	chair_hdrs,a2
	movk	10,a4
#hdr
	move	*a3+,a1,L
	move	*a1+,*a2+,L		;SIZX,SIZY
	move	*a1+,*a2+,L		;ANIX,ANIY
	move	*a1+,*a2+,L		;SAG
	move	*a1+,*a2+,W		;CTRL
	movi	CHAIR_P,a14
	move	a14,*a2+,L		;ICMAP
	addk	#HDR-90h,a2
	dsj	a4,#hdr
	PULL	a1,a2,a3,a4,a14
	rets

chair_src
	.long	CHSWNG01,CHSWNG02,CHSWNG03,CHSWNG04,CHSWNG05
	.long	CHSWNG06,CHSWNG07,CHSWNG08,CHSWNG09,CHSWNFL

;The table the old scripts attach from (DNKSEQ3.ASM has it commented out):
;FR1-FR9 are CHSWNG01-09, FR10 is CHSWNFL.
CHAIR_SWING
	.long	0
	.long	chair_hdrs+0*#HDR,chair_hdrs+1*#HDR,chair_hdrs+2*#HDR
	.long	chair_hdrs+3*#HDR,chair_hdrs+4*#HDR,chair_hdrs+5*#HDR
	.long	chair_hdrs+6*#HDR,chair_hdrs+7*#HDR,chair_hdrs+8*#HDR
	.long	chair_hdrs+9*#HDR

******************************************************************************
* A hit: the commentator. "Yes, the chair!" and "The chair!" (SOUND.EQU
* L_YES_THE_CHAIR, L_THE_CHAIR), which nothing in the original says.

chair_hit
	move	@chair_line,a0
	xori	1,a0
	move	a0,@chair_line
	addi	2ECh,a0
	calla	ADD_VOICE
	rets

	.include	"chairseq.asm"
