**************************************************************
*
* Extra squares on the character select screen, for the mods that bring a
* wrestler of their own (mods/adambomb, mods/dink). Not a mod itself: while no
* mod switches a square on (xsq_on is zero) the select screen is the original's
* (src/wolf/core.gen.txt). Up to two squares: A (Adam Bomb) and B (Dink). One
* stands under the eight in the middle; with both, A on the left and B on the
* right, a fifth row of the grid. Their index on the screen is 8 (and 9).
*
* A mod fills its square's entries every frame (the game clears RAM while it
* starts), from C: wolf_symbol_addr("xsq_wnum") + 16 * square and so on.
*
**************************************************************

	.file	"xsquare.asm"
	.title	"extra select squares"
	.width	132
	.option	b,d,l,t
	.mnolist

	.include	"macros.h"
	.include	"mproc.equ"
	.include	"display.equ"
	.include	"gsp.equ"
	.include	"sys.equ"
	.include	"game.equ"

	.ref	crouton_pos_table,scramble_table,BEGINOBJW

	BSSX	xsq_on,16		;bit 0: square A, bit 1: square B
	BSSX	xsq_wnum,32		;per square (word): the wrestler number chosen
	BSSX	xsq_small,32		;per square (word): non-zero = he is the small one (Dink)
	BSSX	xsq_crut,64		;per square (long): * crouton image
	BSSX	xsq_mug,64		;per square (long): * mugshot pieces, 0-terminated
	BSSX	xsq_name,64		;per square (long): * name image
	BSSX	xsq_smallp,16		;bit per player number: he chose a small one

	.text
	.even

;positions of the eight while there are extra squares: moved up, 44 apart
 SUBR	xsq_pos8
	.word	164,24
	.word	204,24
	.word	164,68
	.word	204,68
	.word	164,112
	.word	204,112
	.word	164,156
	.word	204,156
	.word	0
;one extra square, in the middle under them
 SUBR	xsq_pos1
	.word	184,200
;two, side by side
 SUBR	xsq_pos2
	.word	164,200
	.word	204,200


******************************************************************************
* Number of extra squares in a14 (0-2).

 SUBR	xsq_count

	move	@xsq_on,a14
	cmpi	3,a14
	jreq	#two4
	move	a14,a14
	jrz	#done4
	movk	1,a14
#done4	rets
#two4	movk	2,a14
	rets


******************************************************************************
* a0 = index on the screen (8 or 9) -> a0 = square (0 = A, 1 = B). Trashes a14.

 SUBR	xsq_slot

	move	@xsq_on,a14
	cmpi	3,a14
	jreq	#both5
	srl	1,a14			;one: B if it is B's bit
	move	a14,a0
	rets
#both5	subk	8,a0
	rets


******************************************************************************
* a1 (a6) = index * 32: adds the address of the square's position in its
* table, the original's while there are no extra squares. Trashes a14.

 SUBR	xsq_cpos

	PUSH	a0
	move	a1,a0
	callr	#pos7
	move	a0,a1
	PULL	a0
	rets

 SUBR	xsq_cpos6

	PUSH	a0
	move	a6,a0
	callr	#pos7
	move	a0,a6
	PULL	a0
	rets

#pos7	move	@xsq_on,a14
	jrnz	#ext7
	addi	crouton_pos_table,a0
	rets
#ext7	cmpi	8*32,a0
	jrge	#extra7
	addi	xsq_pos8,a0
	rets
#extra7	subi	8*32,a0
	move	@xsq_on,a14
	cmpi	3,a14
	jreq	#p27
	addi	xsq_pos1,a0
	rets
#p27	addi	xsq_pos2,a0
	rets


******************************************************************************
* The select screen's choice (SELECT.ASM #but_hit): a14 = index * 16, a11 =
* index, a10 = player. Returns in a14 the address of the word with the
* wrestler number: the original's scramble_table, or the square's. Sets the
* player's bit in xsq_smallp when he takes a small one, clears it otherwise.

 SUBR	xsq_choose

	addi	scramble_table,a14
	PUSH	a0,a1
	move	@xsq_on,a0
	jrz	#done8
	cmpi	8,a11
	jrge	#extra8
	clr	a1			;one of the eight
	jruc	#flag8
#extra8	move	a11,a0
	callr	xsq_slot
	sll	4,a0
	move	a0,a14
	addi	xsq_wnum,a14
	addi	xsq_small,a0
	move	*a0,a1
#flag8	move	a10,a0
	callr	xsq_setsmall
#done8	PULL	a0,a1
	rets


******************************************************************************
* a0 = index on the screen, a1 = player number: returns a0 = the wrestler
* number there, and (while there are extra squares) sets or clears the
* player's bit in xsq_smallp. Trashes a14.

 SUBR	xsq_wrestler

	PUSH	a1,a2
	move	a1,a2
	cmpi	8,a0
	jrge	#ext9
	sll	4,a0
	addi	scramble_table,a0
	move	*a0,a0,W
	clr	a1
	jruc	#flag9
#ext9	callr	xsq_slot
	sll	4,a0
	move	a0,a1
	addi	xsq_small,a1
	move	*a1,a1
	addi	xsq_wnum,a0
	move	*a0,a0
#flag9	move	@xsq_on,a14
	jrz	#done9
	PUSH	a0
	move	a2,a0
	callr	xsq_setsmall
	PULL	a0
#done9	PULL	a1,a2
	rets


******************************************************************************
* a0 = player number, a1 = non-zero: he is the small one. Trashes a0.

 SUBR	xsq_setsmall

	PUSH	a1,a2
	movk	1,a2
	sll	a0,a2
	move	@xsq_smallp,a0
	andn	a2,a0
	move	a1,a1
	jrz	#clr10
	or	a2,a0
#clr10	move	a0,@xsq_smallp
	PULL	a1,a2
	rets


******************************************************************************
* a0 = index (8 or 9): returns a0 = * mugshot pieces of the square there.

 SUBR	xsq_mugs

	callr	xsq_slot
	sll	5,a0
	addi	xsq_mug,a0
	move	*a0,a0,L
	rets

;the same with its name image
 SUBR	xsq_names

	callr	xsq_slot
	sll	5,a0
	addi	xsq_name,a0
	move	*a0,a0,L
	rets


******************************************************************************
* Draws the croutons of the extra squares (SELECT.ASM display_croutons).

 SUBR	xsq_croutons

	PUSH	a8,a9,a10,a11
	callr	xsq_count
	move	a14,a9			;how many
	jrz	#done13
	movk	8,a10			;index
#lp13	move	a10,a1
	sll	5,a1
	callr	xsq_cpos
	move	*a1+,a11
	move	*a1,a1
	sll	16,a11
	sll	16,a1
	move	a10,a0
	callr	xsq_slot
	sll	5,a0
	addi	xsq_crut,a0
	move	*a0,a2,L
	move	a11,a0
	movi	4,a3			;as the others (#crutpic_z13)
	movi	DMAWNZ,a4
	movi	CLSNEUT,a5
	clr	a6
	clr	a7
	calla	BEGINOBJW
	inc	a10
	dsj	a9,#lp13
#done13	PULL	a8,a9,a10,a11
	rets


******************************************************************************
* mods/fourplayer: a square of player 3 or 4 moves over all the squares.
* a6 = its position, a2 = the new presses (bits 0-3 up, down, left, right).
* Returns the new position in a6. Up from the top row and down from the
* bottom wrap round, as fourplayer's own moves do.

 SUBR	xsq_fp_move

	PUSH	a1
	callr	xsq_count
	move	a14,a1			;extra squares
	cmpi	8,a6
	jrlt	#lr14
	cmpi	1,a1
	jreq	#updown14			;the one in the middle: no left or right
#lr14	btst	2,a2			;left
	jrz	#nl14
	andni	1,a6
#nl14	btst	3,a2			;right
	jrz	#updown14
	ori	1,a6
#updown14	btst	0,a2			;up
	jrz	#nu14
	cmpi	8,a6
	jrlt	#u114
	andi	1,a6			;from an extra square to the row above
	addk	6,a6
	jruc	#nu14
#u114	subk	2,a6
	jrge	#nu14
	andi	1,a6			;wrap round to the bottom: the square below the column
	addk	8,a6
	cmpi	2,a1
	jreq	#nu14
	movk	8,a6			;or the one in the middle
#nu14	btst	1,a2			;down
	jrz	#nd14
	cmpi	8,a6
	jrlt	#d114
	andi	1,a6			;wrap round to the top
	jruc	#nd14
#d114	addk	2,a6
	cmpi	8,a6
	jrlt	#nd14
	cmpi	2,a1
	jreq	#nd14			;two: 6 and 7 go to 8 and 9
	movk	8,a6
#nd14	PULL	a1
	rets

	.end
