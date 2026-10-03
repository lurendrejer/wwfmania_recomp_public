**************************************************************
*
* Four players: the buddies of buddy mode are played by people
* (mods/fourplayer/gen.txt)
*
**************************************************************

	.file	"fourplayer.asm"
	.title	"four players"
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
	.include	"imgtbl.glo"

	;Set by the machine while the mod is switched on.
	BSSX	fp_on,16

	;The third and fourth person's controls, one 16-bit word each: bits 0-3 the stick
	;(MOVE_UP, DOWN, LEFT, RIGHT), bits 4-8 the buttons in DRN_BUT order (punch, block,
	;super punch, kick, super kick), bit 9 start. The machine writes them every frame.
	BSSX	fp_in,32

	;The wrestlers players 3 and 4 picked on the select screen (-1 = none: the game picks).
	BSSX	fp_pick3,16
	BSSX	fp_pick4,16

	;Non-zero while a player who has started to choose has not finished (the select screen waits).
	BSSX	fp_wait,16

	;Select screen state, one 16-bit word per player: cursor position (0-7, two columns of
	;four), the last input word, and bit 0 = chosen, bit 1 = has joined.
	BSSX	fp_pos,32
	BSSX	fp_prev,32
	BSSX	fp_state,32
	BSSX	fp_obj,64		;the cursor objects (one long each; 0 until the player has joined)
	BSSX	fp_cnt,32		;ticks to the next white flash of each square
	BSSX	fp_join,32		;one word per player: non-zero once the player has pressed start
	BSSX	fp_pal,32		;the colour maps of the squares of players 3 and 4
	BSSX	fp_mugs,2*8*32		;the 8 pieces (objects) of the mugshot of players 3 and 4
	BSSX	fp_nobj,64		;the name object of players 3 and 4
	BSSX	fp_drawn,32		;the square position the mugshot of players 3 and 4 shows (-1 none)
	BSSX	fp_mdata,4*32		;the life bar and name objects of players 3 and 4 (LIFEBAR.ASM keeps those of 1 and 2)
	BSSX	fp_who,4*16		;set by the machine: the person (0-3, -1 none) at each place: players 1, 2, partners 1, 2
	BSSX	fp_lbl,8*32		;select screen: the "P" and digit objects of each place's label
	BSSX	fp_lshow,4*16		;the person each label shows
	BSSX	fp_tpal,4*16		;the palettes of the cursors of players 1 and 2 in team colours (0 = not fetched)
	BSSX	fp_has,16		;set by the machine: bit 0 / 1 = someone plays the left / right partner
	BSSX	fp_req,16		;set by the machine: someone has joined as a partner during a match without partners
	BSSX	fp_spk,2*32		;per player of 3 and 4: the wrestler's number, then how long to hold the announcer (call_wrestler_name reads both)
	BSSX	fp_mlast,32		;the wrestler whose name each bottom bar shows (-1 none)

	.ref	scramble_table,crouton_pos_table,pal_getf,FLASHME
	.ref	xsq_on,xsq_cpos6,xsq_fp_move,xsq_wrestler,xsq_mugs
	.ref	wrestler_mugs,BEGINOBJW,DELOBJ,civanic,BEGINOBJ_TBL
	.ref	buddy_mode_on,process_ptrs,life_data,royal_rumble
	.ref	choiceBMOD,ELBKHDRS,ELBKPALS
	.ref	game_interrupt,CURRENT_LADDER
	.ref	digits_tbl,BEGINOBJP
	.ref	triple_sound,call_wrestler_name,GETPRC
	.ref	CRHI_R_P,CRHI_B_P,CRPL_R_P,CRPL_B_P,PCNT
hilite_z	.equ	5		;SELECT.ASM
mugshot_z	.equ	1
name_z		.equ	9

	.text
	.even

#*****************************************************************************
******************************************************************************
* Called at the start of drone_main.
*
* A partner is played by the computer until its person presses start (on the select
* screen, or any time in the match); from then on fp_join is set and the person plays it.
*
* >a13 = *Plyr process
* <a0 = 0 if the drone is the computer's; 1 if a person plays it, and then
*       DRN_JOY and DRN_BUT hold what the person is pressing
* Trashes a0-a2, a14 (a6 and a7, drone_main's old buttons and stick, are kept)

 SUBR	fp_input

	move	@fp_on,a0
	jrz	#no
	move	@buddy_mode_on,a0	;processes 2 and 3: the partners in buddy mode, the two wrestlers on
	jrnz	#ok			;the right in the royal rumble (cooperative), each new one that comes
	move	@royal_rumble,a0	;in takes the place; in the one-player ladder the computer's
	jrz	#no
#ok
	move	*a13(PLYRNUM),a14
	subk	2,a14			;processes 2 and 3 are the buddies
	jrlt	#no
	cmpi	2,a14
	jrge	#no
	sll	4,a14			;16 bits each
	move	a14,a1
	addi	fp_in,a14
	move	*a14,a0			;a0 = what the person presses
	movi	fp_join,a14
	add	a1,a14			;a14 = *this player's join word
	btst	9,a0			;start
	jrz	#nostart
	move	*a14,a2
	jrnz	#nostart		;(already in)
	movk	1,a2
	move	a2,*a14
	PUSH	a0,a1,a14
	calla	fp_startfx		;the start sound players 1 and 2 get
	PULL	a0,a1,a14
#nostart
	move	*a14,a2
	jrz	#no			;has not joined: the computer plays
	move	a0,a14
	sll	32-4,a14
	srl	32-4,a14
	move	a14,*a13(DRN_JOY)
	srl	4,a0
	andi	1Fh,a0
	move	a0,*a13(DRN_BUT)
	movk	1,a0
	rets
#no	clr	a0
	rets

#*****************************************************************************
******************************************************************************
* The select screen for players 3 and 4. A player joins by pressing start and gets a
* square on the grid of the wrestlers (green for player 3, yellow for player 4, flashing
* white like the cursors of players 1 and 2; no mugshot or name). Stick moves it, any
* button chooses. The choice is in fp_pick3 / fp_pick4 (a wrestler number, as index1 and
* index2). A player who does not join is not waited for (the computer plays the partner
* and the game picks its wrestler), nor when player 1 or 2 is not in.
* Created by select_screen (gen.txt); it ends when the screen is wiped.

 SUBR	fp_select

	move	@fp_on,a0
	jrz	#die

	movi	-1,a0
	move	a0,@fp_pick3
	move	a0,@fp_pick4
	clr	a0
	move	a0,@fp_wait
	move	a0,@fp_state
	move	a0,@fp_state+16
	move	a0,@fp_prev
	move	a0,@fp_prev+16
	move	a0,@fp_join
	move	a0,@fp_join+16
	move	a0,@fp_obj,L
	move	a0,@fp_obj+32,L
	movk	2,a0
	move	a0,@fp_pos
	movk	3,a0
	move	a0,@fp_pos+16
	movk	1,a0
	move	a0,@fp_cnt
	move	a0,@fp_cnt+16

	movi	-1,a0			;the mugshots and names of 3 and 4 (the squares' own, 4 slots on the screen)
	move	a0,@fp_drawn
	move	a0,@fp_drawn+16
	clr	a0
	movi	fp_mugs,a1
	movk	16,a2
#zm	move	a0,*a1+,L
	dsj	a2,#zm
	clr	a10
#nm	movi	fp_nmtab,a0
	calla	BEGINOBJ_TBL
	move	a10,a0
	sll	5,a0
	addi	fp_nobj,a0
	move	a8,*a0,L
	move	a10,a0
	addk	2,a0
	sll	5,a0
	addi	fp_namepos,a0
	move	*a0+,a1,W
	move	*a0,a0,W
	calla	fp_scale
	inc	a10
	cmpi	2,a10
	jrlt	#nm

	calla	fp_mklabels
	clr	a0
	move	a0,@fp_tpal,L
	move	a0,@fp_tpal+32,L

	movi	#pal_g,a0		;the colours of the squares (like CRHI_B_P: shadow, colour, white)
	calla	pal_getf
	move	a0,@fp_pal
	movi	#pal_y,a0
	calla	pal_getf
	move	a0,@fp_pal+16

#lp	SLEEPK	1
	clr	a10
#pl	move	a10,a9
	sll	4,a9			;bit offset of this player's words

	movi	fp_in,a0
	add	a9,a0
	move	*a0,a2			;a2 = the inputs now
	movi	fp_prev,a0
	add	a9,a0
	move	*a0,a3
	move	a2,*a0
	movi	fp_state,a0
	add	a9,a0
	move	*a0,a4			;a4 = state
	btst	1,a4
	jrnz	#joined

	btst	9,a2			;start joins
	jrz	#next
	ori	2,a4
	movi	fp_join,a0
	add	a9,a0
	movk	1,a1
	move	a1,*a0
	PUSH	a2,a3,a4,a9,a10
	calla	fp_startfx		;the start sound players 1 and 2 get
	PULL	a2,a3,a4,a9,a10
	movi	#hi_b,a0		;the image is the blue one; the colours are replaced below
	calla	BEGINOBJ_TBL
	move	a10,a0
	sll	5,a0
	addi	fp_obj,a0
	move	a8,*a0,L
	movi	fp_pal,a0
	add	a9,a0
	move	*a0,a0
	jrz	#joined
	move	a0,*a8(OPAL)

#joined	andn	a3,a2			;a2 = new presses only
	btst	0,a4
	jrnz	#done			;already chosen

	move	a2,a5
	srl	4,a5
	andi	1Fh,a5
	jrnz	#choose			;a button (not start)

	movi	fp_pos,a0
	add	a9,a0
	move	*a0,a6
	move	@xsq_on,a14		;extra squares on the screen (src/wolf/asm/XSQUARE.ASM)
	jrz	#nox
	calla	xsq_fp_move
	jruc	#nd
#nox	btst	2,a2			;left
	jrz	#nl
	andni	1,a6
#nl	btst	3,a2			;right
	jrz	#nr
	ori	1,a6
#nr	btst	0,a2			;up
	jrz	#nu
	subk	2,a6
	jrge	#nu
	addk	8,a6
#nu	btst	1,a2			;down
	jrz	#nd
	addk	2,a6
	cmpi	8,a6
	jrlt	#nd
	subk	8,a6
#nd	movi	fp_pos,a0
	add	a9,a0
	move	*a0,a1
	cmp	a6,a1
	jreq	#nomv
	move	a6,*a0
	calla	fp_movefx		;the cursor sound of players 1 and 2
#nomv	jruc	#done

#choose	movi	fp_pos,a0
	add	a9,a0
	move	*a0,a6
	PUSH	a0,a1
	move	a6,a0
	move	a10,a1
	addk	2,a1			;player 3 or 4 is wrestler process 2 or 3
	calla	xsq_wrestler		;position -> wrestler number, as SELECT.ASM does
	move	a0,a6
	PULL	a0,a1		;position -> wrestler number, as SELECT.ASM does
	move	a10,a10
	jrnz	#c4
	move	a6,@fp_pick3
	jruc	#cd
#c4	move	a6,@fp_pick4
#cd	calla	fp_pickfx		;the select sound and the announcer, as for players 1 and 2
	ori	1,a4
	PUSH	a4,a9,a10		;the square flashes white for a while, as the cursors of players 1 and 2 do
	move	a10,a0
	sll	5,a0
	addi	fp_obj,a0
	move	*a0,a8,L
	CREATE0	fp_pickflash
	PULL	a4,a9,a10

#done	movi	fp_state,a0
	add	a9,a0
	move	a4,*a0

	;a partner who is in and has not chosen has the wrestler under the square, as players 1 and 2
	;do: when the select screen's time runs out that is the one they get
	btst	1,a4
	jrz	#nomug
	btst	0,a4
	jrnz	#picked
	movi	fp_pos,a0
	add	a9,a0
	move	*a0,a6
	PUSH	a0,a1
	move	a6,a0
	move	a10,a1
	addk	2,a1			;player 3 or 4 is wrestler process 2 or 3
	calla	xsq_wrestler		;position -> wrestler number, as SELECT.ASM does
	move	a0,a6
	PULL	a0,a1
	move	a10,a10
	jrnz	#t4
	move	a6,@fp_pick3
	jruc	#picked
#t4	move	a6,@fp_pick4
#picked

	;the mugshot and the name follow the square
	movi	fp_pos,a0
	add	a9,a0
	move	*a0,a6
	movi	fp_drawn,a0
	add	a9,a0
	move	*a0,a1,W
	cmp	a6,a1
	jreq	#nomug
	move	a6,*a0
	PUSH	a4,a9,a10
	move	a10,a0
	calla	fp_showmug
	PULL	a4,a9,a10
#nomug

	;place the square
	move	a10,a0
	sll	5,a0
	addi	fp_obj,a0
	move	*a0,a8,L
	movi	fp_pos,a0
	add	a9,a0
	move	*a0,a6
	sll	5,a6
	calla	xsq_cpos6
	move	*a6+,a2,W
	move	*a6,a3,W
	move	a2,*a8(OXPOS)
	move	a3,*a8(OYPOS)

	;flash white every eighth tick, like the cursors of players 1 and 2, until the wrestler is chosen
	btst	0,a4
	jrnz	#next
	movi	fp_cnt,a0
	add	a9,a0
	move	*a0,a1
	dec	a1
	jrp	#nf
	movk	8,a1
	move	a1,*a0
	PUSH	a4,a9,a10
	movi	0202h,a9
	CREATE0	FLASHME
	PULL	a4,a9,a10
	jruc	#next
#nf	move	a1,*a0

#next	inc	a10
	cmpi	2,a10
	jrlt	#pl

	calla	fp_labels

	;the select screen waits while a player who has joined has not chosen
	clr	a0
	move	@PSTATUS,a1
	cmpi	3,a1
	jrne	#nowait			;buddies only exist when both players 1 and 2 are in
	move	@fp_state,a1
	cmpi	2,a1
	jreq	#wait
	move	@fp_state+16,a1
	cmpi	2,a1
	jrne	#nowait
#wait	movk	1,a0
#nowait	move	a0,@fp_wait
	jruc	#lp

#die	DIE


#hi_b	.word	0,0,hilite_z
	.long	CRUTHI_B
	.word	DMAWNZ|M_3D|M_SCRNREL
	.word	CLSNEUT|TYPNEUT
	.long	0,0

;palettes like CRHI_B_P: colour count, then the shadow, the colour and white (the flash uses white)
#pal_g	.word	3,02D6Bh,07D40h,07FFFh	;left partner: a lighter red (more orange than pink)
#pal_y	.word	3,02D6Bh,01A5Fh,07FFFh	;right partner: a lighter blue

#*****************************************************************************
******************************************************************************
* The sounds players 1 and 2 get on the select screen (SELECT.ASM, WRESTLE.ASM), for players 3 and 4.
* The left partner has player 1's, the right player 2's.

* Start (a player joins): WRESTLE.ASM #set_pstatus_and_die plays 49h.
* Trashes a0, a14

 SUBR	fp_startfx

	movi	49h,a0
	calla	triple_sound
	rets

* The square of a player who has chosen flashes white over the picture, SELECT.ASM's "flash over the crouton"
* (the constant colour on the empty pixels of the square, on and off every two ticks).
* >a8 = the square

 SUBR	fp_pickflash

	move	@PCNT,a14		;begin flashing on an even PCNT, as SELECT.ASM
	btst	0,a14
	jrnz	#synch
	SLEEPK	1
#synch	movi	TSEC/4,a9
#fl	movi	0202h,a14
	move	a14,*a8(OCONST)
	move	*a8(OCTRL),a14
	xori	M_CONZER,a14
	move	a14,*a8(OCTRL)
	SLEEPK	2
	dsj	a9,#fl

	move	*a8(OCTRL),a14
	andni	M_CONZER,a14
	move	a14,*a8(OCTRL)
	DIE

* The cursor moves: #PI_MOVESOUND of SELECT.ASM's #p1info / #p2info (0C8h, 0C7h).
* >a10 = 0 left, 1 right. Trashes a14

 SUBR	fp_movefx

	PUSH	a0
	movi	0C8h,a0
	move	a10,a10
	jrz	#ml
	movi	0C7h,a0
#ml	calla	triple_sound
	PULL	a0
	rets

* A wrestler is chosen: the select sound (#PI_SELSOUND, 0CBh, 0CCh) and the announcer says his name
* (SELECT.ASM's call_wrestler_name, in a process of its own as there).
* >a10 = 0 left, 1 right, a6 = the wrestler number (the extra squares of other mods have none to say)
* Trashes a14

 SUBR	fp_pickfx

	PUSH	a0,a1,a7,a8
	movi	0CBh,a0
	move	a10,a10
	jrz	#pl
	movi	0CCh,a0
#pl	calla	triple_sound
	cmpi	8,a6
	jrgt	#px
	move	a10,a8
	sll	5,a8
	addi	fp_spk,a8
	move	a6,*a8			;the number
	movi	60,a0
	move	a0,*a8(10h)		;call_wrestler_name sleeps this long after the call
	movi	SEL_SPCH_PID1,a1
	add	a10,a1
	movi	call_wrestler_name,a7,L
	calla	GETPRC
#px	PULL	a0,a1,a7,a8
	rets

******************************************************************************
* The select screen has four slots for mugshots: players 1 and 3 on the left, 2 and 4 on the
* right, each at half size (anchor points in original pixels: y, x).

MUGX_L	.equ	49
MUGX_R	.equ	351
MUGY_T	.equ	102
MUGY_B	.equ	190
NAMX_L	.equ	78
NAMX_R	.equ	322
NAMY_T	.equ	107
NAMY_B	.equ	195

fp_mugpos
	.word	MUGY_T,MUGX_L,MUGY_T,MUGX_R,MUGY_B,MUGX_L,MUGY_B,MUGX_R
fp_namepos
	.word	NAMY_T,NAMX_L,NAMY_T,NAMX_R,NAMY_B,NAMX_L,NAMY_B,NAMX_R

fp_nmtab	.word	0,0,name_z
	.long	WF_START
	.word	DMAWNZ|M_3D|M_SCRNREL
	.word	CLSNEUT|TYPNEUT
	.long	0,0

#*****************************************************************************
******************************************************************************
* Puts an object at half size with its anchor point at a given place (the anchor point is where the
* game would have put it at full size: the image's own offset (ODXOFF, ODYOFF) shrinks with it).
* The pieces of a picture are drawn each at ceil(size / 2) so that no seam is left between them
* (the DMA rounds a scaled size down), and each starts at floor(its offset / 2).
* A flipped picture is drawn from its far edge, which the display code takes from the unscaled
* width while the DMA takes the scaled one: that is allowed for below.
* >a8 = the object, a0 = x, a1 = y
* Trashes a0, a1

 SUBR	fp_scale

	PUSH	a2,a3,a4,a5,a6,a7
	move	*a8(OSIZEX),a5,W	;width
	move	*a8(OSIZEY),a6,W	;height
	move	a5,a4
	calla	fp_half
	move	a4,a7
	move	a6,a4
	calla	fp_half
	sll	16,a4
	or	a4,a7
	move	a7,*a8(OSCALE),L	;Y:X scale
	move	*a8(ODXOFF),a2,W
	move	*a8(OCTRL),a3,W
	btst	B_FLIPH,a3
	jrz	#nofl
	move	a2,a3
	addk	1,a3
	sub	a5,a3
	sra	1,a3			;floor((offset + 1 - width) / 2)
	sub	a2,a3
	subk	1,a3
	move	a5,a4
	addk	1,a4
	srl	1,a4			;the scaled width
	add	a4,a3
	move	a3,a2
	jruc	#xd
#nofl	sra	1,a2
#xd	add	a0,a2
	move	a2,*a8(OXPOS)
	move	*a8(ODYOFF),a2,W
	sra	1,a2
	add	a1,a2
	move	a2,*a8(OYPOS)
	PULL	a2,a3,a4,a5,a6,a7
	rets

* The DMA scale (8.8, larger shrinks) that makes a size ceil(size / 2).
* >a4 = size, <a4 = scale

 SUBR	fp_half

	PUSH	a3,a5
	move	a4,a3
	addk	1,a3
	srl	1,a3
	move	a4,a5
	sll	8,a5
	clr	a4
	divu	a3,a4
	PULL	a3,a5
	rets

#*****************************************************************************
******************************************************************************
* Called by #draw_mugshot (gen.txt) for each piece of the mugshot of player 1 or 2.
* >a8 = the piece, a9 = the player's info (+40h: start index, 0 for player 1, 1 for player 2)

 SUBR	fp_mug1

	move	@fp_on,a0
	jrz	#x
	move	*a9(40h),a0,W
	sll	5,a0
	addi	fp_mugpos,a0
	move	*a0+,a1,W
	move	*a0,a0,W
	calla	fp_scale
#x	rets

#*****************************************************************************
******************************************************************************
* Called by #update_name (gen.txt) after the name of player 1 or 2 has changed: with the mod on the
* select screen shows no names (the mugshots have the labels), so the name object goes off the screen.
* >a8 = the name object

 SUBR	fp_name1

	move	@fp_on,a0
	jrz	#x
	calla	fp_hide
#x	rets

* Puts an object far outside the screen.
* >a8 = the object

 SUBR	fp_hide

	movi	-4000,a0
	move	a0,*a8(OXPOS)
	rets

#*****************************************************************************
******************************************************************************
* Draws the mugshot and name of the wrestler player 3 or 4 has under the square.
* >a0 = 0 (player 3) or 1 (player 4); fp_drawn holds the square position

 SUBR	fp_showmug

	PUSH	a2,a3,a4,a5,a6,a7,a8,a9,a10,a11
	move	a0,a10
	move	a10,a9
	sll	8,a9
	addi	fp_mugs,a9		;a9 = this player's 8 piece objects
	move	a9,a5
	movk	8,a6
#er	move	*a5,a0,L
	jrz	#ne
	calla	DELOBJ
	clr	a0
	move	a0,*a5,L
#ne	addk	32,a5
	dsj	a6,#er

	move	a10,a0			;the list of pieces of the wrestler
	sll	4,a0
	addi	fp_drawn,a0
	move	*a0,a0,W
	cmpi	8,a0
	jrlt	#mug8
	calla	xsq_mugs		;an extra square's (src/wolf/asm/XSQUARE.ASM)
	move	a0,a11
	jruc	#mugx
#mug8	sll	4,a0
	addi	scramble_table,a0
	move	*a0,a0,W
	sll	5,a0
	addi	wrestler_mugs,a0
	move	*a0,a11,L
#mugx
	move	*a11+,a2,L
	jrz	#nm
#dl	clr	a0
	clr	a1
	movi	mugshot_z,a3
	movi	DMAWNZ|M_3D|M_SCRNREL,a4
	move	a10,a10
	jrz	#nofl
	movi	DMAWNZ|M_3D|M_SCRNREL|M_FLIPH,a4	;player 4 is on the right, like player 2
#nofl	movi	CLSNEUT,a5
	clr	a6
	clr	a7
	calla	BEGINOBJW
	move	a8,*a9+,L
	move	a10,a0
	addk	2,a0
	sll	5,a0
	addi	fp_mugpos,a0
	move	*a0+,a1,W
	move	*a0,a0,W
	calla	fp_scale
	move	*a11+,a2,L
	jrnz	#dl

#nm	move	a10,a8			;no name: the "PRESS START" of the place goes (it is the name object)
	sll	5,a8
	addi	fp_nobj,a8
	move	*a8,a8,L
	calla	fp_hide
	PULL	a2,a3,a4,a5,a6,a7,a8,a9,a10,a11
	rets

#*****************************************************************************
* The life bars of players 3 and 4: along the bottom of the screen, as those of players 1 and 2
* are along the top (LIFEBAR.ASM). Player 3 is on the left, player 4 on the right.

fpb_lx		.equ	11
fpb_rx		.equ	390
fpb_y		.equ	236
fpb_ny		.equ	229
fpb_lnx		.equ	97
fpb_rnx		.equ	303
fpb_bak_z	.equ	210+1800h	;in front of the ropes, where LIFEBAR.ASM brings its own bars when they would be
fpb_frame_z	.equ	211+1800h	;hidden (SHIFT_BARS_IN_Z), but above its range (200-203 + 1800h): it shifts what is in
fpb_bar_z	.equ	212+1800h	;that range back at the end of a round
fpb_name_z	.equ	213+1800h
PLT_CLIFE	.equ	10h		;LIFEBAR.ASM: displayed life, in the player's life_data block (40h bits each)
LIFE_MAX	.equ	163

* Called by meters (gen.txt) while it makes the meters of a match.

 SUBR	fp_mtr_init

	mmtm	sp,a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11
	move	@fp_on,a0
	jrz	#x
	move	@buddy_mode_on,a0
	jrz	#x
	clr	a0
	move	a0,@fp_mdata,L
	move	a0,@fp_mdata+64,L
	move	@process_ptrs+40h,a0,L	;no bar for a partner who is not in the match
	jrz	#nol
	movi	fp_ll,a0
	calla	BEGINOBJ_TBL
	move	a8,@fp_mdata,L
	movi	fp_ln,a0
	calla	BEGINOBJ_TBL
	move	a8,@fp_mdata+32,L
	movi	fp_lc,a0
	calla	BEGINOBJ_TBL
	movi	fp_lf,a0
	calla	BEGINOBJ_TBL
	movi	fp_lb,a0
	calla	BEGINOBJ_TBL
#nol	move	@process_ptrs+60h,a0,L
	jrz	#nor
	movi	fp_rl,a0
	calla	BEGINOBJ_TBL
	move	a8,@fp_mdata+64,L
	movi	fp_rn,a0
	calla	BEGINOBJ_TBL
	move	a8,@fp_mdata+96,L
	movi	fp_rc,a0
	calla	BEGINOBJ_TBL
	movi	fp_rf,a0
	calla	BEGINOBJ_TBL
	movi	fp_rb,a0
	calla	BEGINOBJ_TBL
#nor	movi	-1,a0
	move	a0,@fp_mlast
	move	a0,@fp_mlast+16
#x	mmfm	sp,a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11
	rets

#*****************************************************************************
* Called by meters every frame: the bars follow the life of the wrestlers (processes 2 and 3) and show their names.

 SUBR	fp_mtr_update

	mmtm	sp,a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11
	move	@fp_on,a0
	jrz	#x

	;someone joined as a partner while the match has none: stop it and go to the select screen,
	;as the game does when a second player comes in during a match (game_interrupt, WRESTLE.ASM)
	move	@fp_req,a0
	jrz	#noreq
	clr	a0
	move	a0,@fp_req
	move	@buddy_mode_on,a0
	jrnz	#noreq
	move	@CURRENT_LADDER,a0,L	;(game_interrupt steps the one-player ladder back; nobody climbs it here)
	addi	20h,a0
	move	a0,@CURRENT_LADDER,L
	CREATE	NO_PID,game_interrupt
#noreq
	clr	a10
#lp	move	a10,a0
	sll	6,a0
	addi	fp_mdata,a0
	move	*a0,a8,L		;the life bar object
	jrz	#next			;(the meters were made without the mod)
	move	a10,a2
	addk	2,a2			;PLYRNUM
	move	a2,a3
	sll	6,a3
	addi	life_data,a3
	move	*a3(PLT_CLIFE),a14
	neg	a14
	addi	LIFE_MAX,a14		;pixels to hide
	move	a14,*a8(OFSET)

	move	a2,a4			;the name
	sll	5,a4
	addi	process_ptrs,a4
	move	*a4,a4,L
	jrz	#next
	move	*a4(WRESTLERNUM),a5
	move	a10,a6
	sll	4,a6
	addi	fp_mlast,a6
	move	*a6,a7,W
	cmp	a5,a7
	jreq	#next
	move	a5,*a6
	move	a5,a0
	sll	5,a0
	addi	fp_lnames,a0
	move	*a0,a0,L
	move	a10,a8
	sll	6,a8
	addi	fp_mdata+32,a8
	move	*a8,a8,L
	calla	civanic
#next	inc	a10
	cmpi	2,a10
	jrlt	#lp
#x	mmfm	sp,a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11
	rets

fp_lnames	;as name_tbl in LIFEBAR.ASM
	.long	NAM_BRT,NAM_RZR,NAM_UND,NAM_YOK,NAM_SHN
	.long	NAM_BAM,NAM_DNK,NAM_DNK,NAM_LEX,NAM_LEX

fp_ll	.word	fpb_lx,fpb_y,fpb_bar_z
	.long	FGRN2YEL
	.word	DMAWNZ|M_3D|M_SCRNREL
	.word	CLSNEUT|TYPNEUT|SUBLBAR
	.long	0,0
fp_rl	.word	fpb_rx,fpb_y,fpb_bar_z
	.long	FGRN2YEL
	.word	DMAWNZ|M_3D|M_SCRNREL|M_FLIPH
	.word	CLSNEUT|TYPNEUT|SUBLBAR
	.long	0,0
fp_ln	.word	fpb_lnx,fpb_ny,fpb_name_z
	.long	NAM_BRT
	.word	DMAWNZ|M_3D|M_SCRNREL
	.word	CLSNEUT|TYPNEUT|SUBLBAR
	.long	0,0
fp_rn	.word	fpb_rnx,fpb_ny,fpb_name_z
	.long	NAM_RZR
	.word	DMAWNZ|M_3D|M_SCRNREL
	.word	CLSNEUT|TYPNEUT|SUBLBAR
	.long	0,0
fp_lc	.word	fpb_lx,fpb_y,fpb_bak_z
	.long	LBAR_GENC
	.word	DMAWNZ|M_3D|M_SCRNREL
	.word	CLSNEUT|TYPNEUT|SUBMFRAME
	.long	0,0
fp_rc	.word	fpb_rx,fpb_y,fpb_bak_z
	.long	LBAR_GENC
	.word	DMAWNZ|M_3D|M_SCRNREL|M_FLIPH
	.word	CLSNEUT|TYPNEUT|SUBMFRAME
	.long	0,0
fp_lf	.word	fpb_lx,fpb_y,fpb_frame_z
	.long	PWREMPTY
	.word	DMAWNZ|M_3D|M_SCRNREL
	.word	CLSNEUT|TYPNEUT|SUBMFRAME
	.long	0,0
fp_rf	.word	fpb_rx,fpb_y,fpb_frame_z
	.long	PWREMPTY
	.word	DMAWNZ|M_3D|M_SCRNREL|M_FLIPH
	.word	CLSNEUT|TYPNEUT|SUBMFRAME
	.long	0,0
fp_lb	.word	fpb_lx,fpb_y,fpb_frame_z
	.long	LBAR_GENB
	.word	DMAWNZ|M_3D|M_SCRNREL
	.word	CLSNEUT|TYPNEUT|SUBMFRAME
	.long	0,0
fp_rb	.word	fpb_rx,fpb_y,fpb_frame_z
	.long	RBAR_GENB
	.word	DMAWNZ|M_3D|M_SCRNREL|M_FLIPH
	.word	CLSNEUT|TYPNEUT|SUBMFRAME
	.long	0,0

#*****************************************************************************
* The select screen's background without the two big panels: the mugshots stand on the stone. The
* stone blocks of the original background (WWFSELBK #0 and #1) cover the screen, and the frames of
* the wrestler grid (#9) stay where they were. SELECT.ASM uses it when the mod is on (gen.txt).

* >a0 = the background module list the select screen is about to use; <a0 = ours when the mod is on.

 SUBR	fp_bkmod

	move	@fp_on,a14
	jrz	#x
	movi	fp_selmod,a0
#x	rets

fp_selmod
	.long	fp_selbkBMOD
	.word	-40,0
	.long	choiceBMOD
	.word	3,256
	.long	0

fp_selbkBMOD
	.word	476,256,38
	.long	fp_selbkBLKS,ELBKHDRS,ELBKPALS

fp_selbkBLKS
	.word	0545H,0,0,00H
	.word	0546H,0,62,01H
	.word	0545H,0,124,00H
	.word	0546H,0,186,01H
	.word	0545H,0,192,00H
	.word	0546H,76,0,01H
	.word	0545H,76,62,00H
	.word	0546H,76,124,01H
	.word	0545H,76,186,00H
	.word	0546H,76,192,01H
	.word	0545H,152,0,00H
	.word	0546H,152,62,01H
	.word	0545H,152,124,00H
	.word	0546H,152,186,01H
	.word	0545H,152,192,00H
	.word	04044H,204,45,09H
	.word	04044H,204,90,09H
	.word	04044H,204,135,09H
	.word	04044H,204,180,09H
	.word	0546H,228,0,01H
	.word	0545H,228,62,00H
	.word	0546H,228,124,01H
	.word	0545H,228,186,00H
	.word	0546H,228,192,01H
	.word	04044H,244,45,09H
	.word	04044H,244,90,09H
	.word	04044H,244,135,09H
	.word	04044H,244,180,09H
	.word	0545H,304,0,00H
	.word	0546H,304,62,01H
	.word	0545H,304,124,00H
	.word	0546H,304,186,01H
	.word	0545H,304,192,00H
	.word	0546H,380,0,01H
	.word	0545H,380,62,00H
	.word	0546H,380,124,01H
	.word	0545H,380,186,00H
	.word	0546H,380,192,01H
	.word	0FFFFH

#*****************************************************************************
* Labels on the select screen: "P1".."P4" at the top left of each mugshot place, the person who
* plays it (the places go to people in the order they press start, so player 3 can be the right-hand
* player). Hidden while nobody has the place.

fp_lblpos	;y, x of the "P" of each place: players 1, 2, partners 1, 2
	.word	33,50,33,290,121,50,121,290

 SUBR	fp_mklabels

	mmtm	sp,a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11
	clr	a10
#lp	move	a10,a9
	sll	6,a9
	addi	fp_lbl,a9		;a9 = this place's two objects
	move	a10,a11
	sll	5,a11
	addi	fp_lblpos,a11
	move	*a11(16),a0,W
	move	*a11,a1,W
	sll	16,a0
	sll	16,a1
	movi	FNT9_P,a2
	callr	#mk
	move	a8,*a9,L
	move	*a11(16),a0,W
	addk	10,a0
	move	*a11,a1,W
	sll	16,a0
	sll	16,a1
	movi	FNT9_1,a2
	callr	#mk
	move	a8,*a9(32),L
	move	a10,a0
	sll	4,a0
	addi	fp_lshow,a0
	movi	-2,a1			;(not shown yet: the first fp_labels sets it)
	move	a1,*a0
	inc	a10
	cmpi	4,a10
	jrlt	#lp
	mmfm	sp,a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11
	rets

#mk	PUSH	a9,a10,a11
	movi	20,a3
	movi	DMAWNZ|M_SCRNREL,a4
	movi	CLSNEUT|TYPNEUT,a5
	clr	a6
	clr	a7
	movi	FNT9YEL_P,b0
	calla	BEGINOBJP
	move	*a8(OFLAGS),a0
	ori	M_NODISP,a0
	move	a0,*a8(OFLAGS)
	PULL	a9,a10,a11
	rets

#*****************************************************************************
* Every tick of the select screen: the labels follow fp_who.

 SUBR	fp_labels

	mmtm	sp,a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11
	clr	a10
#lp	move	a10,a0
	sll	4,a0
	addi	fp_who,a0
	move	*a0,a1,W
	move	a10,a0
	sll	4,a0
	addi	fp_lshow,a0
	move	*a0,a2,W
	cmp	a1,a2
	jreq	#nx
	move	a1,*a0
	move	a10,a9
	sll	6,a9
	addi	fp_lbl,a9
	move	a1,a1
	jrn	#hide
	addk	1,a1
	sll	5,a1
	addi	digits_tbl,a1
	move	*a1,a0,L
	move	*a9(32),a8,L
	calla	civanic
	move	*a9,a8,L
	callr	#show
	move	*a9(32),a8,L
	callr	#show
	jruc	#nx
#hide	move	*a9,a8,L
	callr	#hid
	move	*a9(32),a8,L
	callr	#hid
#nx	inc	a10
	cmpi	4,a10
	jrlt	#lp
	mmfm	sp,a0,a1,a2,a3,a4,a5,a6,a7,a8,a9,a10,a11
	rets

#show	move	*a8(OFLAGS),a0
	andni	M_NODISP,a0
	move	a0,*a8(OFLAGS)
	rets

#hid	move	*a8(OFLAGS),a0
	ori	M_NODISP,a0
	move	a0,*a8(OFLAGS)
	rets

#*****************************************************************************
* Team colours on the select screen: the left side (player 1 and their partner) red, the right
* side blue, the partners in lighter shades (#pal_g, #pal_y). The original has player 1 blue and
* player 2 red; called by #place_cursor (gen.txt) each time a cursor of player 1 or 2 is placed.
* >a7 = the cursor (hilite), a8 = its plate, a10 = player (0 or 1)

fp_tpals	;cursor, plate palette of the left and right side
	.long	CRHI_R_P,CRPL_R_P,CRHI_B_P,CRPL_B_P

 SUBR	fp_curpal

	PUSH	a0,a1,a2,a14
	move	@fp_on,a0
	jrz	#x
	move	a10,a2
	sll	5,a2			;two words a side
	addi	fp_tpal,a2
	move	*a2,a0
	jrnz	#hv
	move	a10,a1
	sll	6,a1
	addi	fp_tpals,a1
	move	*a1,a0,L
	calla	pal_getf
	move	a0,*a2
#hv	move	a0,*a7(OPAL)
	move	*a2(16),a0
	jrnz	#pv
	move	a10,a1
	sll	6,a1
	addi	fp_tpals+32,a1
	move	*a1,a0,L
	calla	pal_getf
	move	a0,*a2(16)
#pv	move	a0,*a8(OPAL)
#x	PULL	a0,a1,a2,a14
	rets
