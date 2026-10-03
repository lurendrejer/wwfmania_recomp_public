**************************************************************
*
* Two people play the one-player ladder together
* (mods/coopladder/gen.txt)
*
**************************************************************

	.file	"coopladder.asm"
	.title	"cooperative ladder"
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

	.ref	royal_rumble,belt_ask,belt_type,is_8_on_1,INIT_LADDER_TABLE,KIL1C,OBJLST,OFREE,pal_clean

	;The mod's number (0 = off: COOP is the royal rumble, 1 = Intercontinental, 2 = WWF
	;championship, 3 = ask). The machine sets it every frame while the mod is switched on.
	BSSX	coop_arg,16

	;Non-zero from the choice of COOP on the question screen until the game is over or only one
	;of the two people continues: the two humans play the ladder against the computer's
	;wrestlers. While it is set, royal_rumble is set too, which puts both humans on the first
	;team with one life bar (the game's cooperative mode).
	BSSX	coop_ladder,16

	.text
	.even

#*****************************************************************************
* COOP has just been chosen on the question screen (royal_rumble is set). If the mod's number
* asks for the ladder, sets coop_ladder (and for 1 and 2 the belt and the ladder).
* Number 3 leaves the belt to the question screen that pregame_show shows next.

 SUBR	coop_question

	clr	a14
	move	a14,@coop_ladder
	move	@royal_rumble,a14
	jrz	#done
	move	@coop_arg,a14
	jrz	#done			;0: the royal rumble, as in the original
	movk	1,a14
	move	a14,@coop_ladder

	;a ladder is already under way (the second person joined): carry on with it
	move	@belt_ask,a14,W
	jrz	#done

	move	@coop_arg,a14
	cmpi	3,a14
	jrlt	#preset
	calla	coop_clear		;3: pregame_show asks for the belt next, on a clear screen
	jruc	#done
#preset	dec	a14			;1 = Intercontinental = belt 0, 2 = WWF = belt 1
	move	a14,@belt_type
	calla	INIT_LADDER_TABLE
	clr	a14
	move	a14,@belt_ask
#done	rets

#*****************************************************************************
* The royal_rumble of the places that are about the line of eight wrestlers (the counter, the
* zombies, the clock, the rounds, the pin and so on), as opposed to the places that are about
* two humans on one team (life bars, camera, names). A ladder match of the cooperative mode is
* a line only in the final battle of the WWF belt (8 against 2).
* Returns the value in a14 with the flags of a MOVE, like the instruction it replaces.

 SUBR	rr_line

	move	@royal_rumble,a14
	jrz	#out
	move	@coop_ladder,a14
	jrz	#line
	calla	is_8_on_1
	jrnc	#not
#line	movk	1,a14
	jruc	#out
#not	clr	a14
#out	move	a14,a14
	rets

#*****************************************************************************
* Zero flag clear if this is the cooperative ladder's 8-on-2 final battle.

 SUBR	coop_final

	move	@coop_ladder,a14
	jrz	#no
	calla	is_8_on_1
	jrnc	#no
	movk	1,a14
	move	a14,a14
	rets
#no	clr	a14
	move	a14,a14
	rets

#*****************************************************************************
* Takes the first question screen away: the objects of the question screens are on the second
* screen of the world (y of 256 and more), and the two palette cyclers and the flashing shadow
* of the screen are still running. The original leaves them for the wipe that follows the
* belt question; here a second question follows, which would draw over them.
* Trashes a0, a1, a2, a14

 SUBR	coop_clear

	movi	PCYC_PID,a0
	calla	KIL1C
	movi	FLASH_PID,a0
	calla	KIL1C
	movi	CLOCK_PID,a0		;the select screen's clock and its countdown digits (clock_digits,
	calla	KIL1C			;created with PID 0): the digits would show over the second question
	clr	a0
	calla	KIL1C
	movi	OBJLST,a2		;a2 = the object before the one looked at
#lp	move	*a2,a0,L
	jrz	#done
	move	*a0(OYPOS),a1
	cmpi	256,a1
	jrlt	#next
	move	*a0,*a2,L		;unlink it (DELOBJ in DISPLAY.ASM)
	clr	a1
	move	a1,*a0(OXPOS)
	move	@OFREE,*a0,L
	move	a0,@OFREE,L
	jruc	#lp
#next	move	a0,a2
	jruc	#lp
#done	calla	pal_clean		;free the palettes of the objects that are gone (the second question needs its own)
	rets
