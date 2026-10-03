**************************************************************
*
* Easy special moves (mods/easymoves/gen.txt)
*
**************************************************************

	.file	"easymoves.asm"
	.title	"easy special moves"
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

	;Non-zero: the extra Toward, Toward + button records do their moves,
	;and the repeat button of the hammer is the super kick. The machine
	;sets it while the mod is switched on.
	BSSX	easy_enabled,16

	;A move started the easy way repeats on the button it was started with, where the original repeats on
	;another: for each wrestler (PLYRNUM), presses of the button in easy_ali_x count as presses of the one in
	;easy_ali_y too (easy_alias_mask, in count_button_presses) for easy_ali_t more ticks.
	BSSX	easy_ali_x,8*16
	BSSX	easy_ali_y,8*16
	BSSX	easy_ali_t,8*16

	.text
	.even

******************************************************************************
* Animation code (ANI_CODE): the hammer repeats while KICKB_COUNT is high
* enough. With the mod the super kick is the repeat button, so its count is
* put where the check looks.
*
* a13 = wrestler process

 SUBR	easy_sync_kick
	move	@easy_enabled,a0
	jrz	#out
	move	*a13(SKICKB_COUNT),a0
	move	a0,*a13(KICKB_COUNT)
#out	rets

******************************************************************************
* An easy move is starting (the record or the process of it): presses of the button in a0 count as presses of the
* one in a1 too, for a while. The masks are PLAYER_*_VAL.
*
* a13 = wrestler process, a0 = the button pressed, a1 = the button the original move looks at
* Trashes a0, a1, a14

 SUBR	easy_alias

	move	*a13(PLYRNUM),a14
	cmpi	8,a14
	jrge	#ea_out
	sll	4,a14
	PUSH	a2
	move	a14,a2
	addi	easy_ali_x,a2
	move	a0,*a2
	move	a14,a2
	addi	easy_ali_y,a2
	move	a1,*a2
	move	a14,a2
	addi	easy_ali_t,a2
	movi	240,a0			;about four seconds: longer than the repeats of any move wait
	move	a0,*a2
	PULL	a2
#ea_out	rets

******************************************************************************
* Called from count_button_presses (gen.txt) right after it got the buttons that went down: adds the alias.
*
* a13 = wrestler process, a0 = the buttons that went down
* <a0 = the same, with the alias added
* Trashes a14

 SUBR	easy_alias_mask

	move	@easy_enabled,a14
	jrz	#eam_out
	move	*a13(PLYRNUM),a14
	cmpi	8,a14
	jrge	#eam_out
	sll	4,a14
	PUSH	a1,a2
	move	a14,a2
	addi	easy_ali_t,a2
	move	*a2,a1
	jrz	#eam_done
	dec	a1
	move	a1,*a2
	move	a14,a2
	addi	easy_ali_x,a2
	move	*a2,a1
	and	a0,a1
	jrz	#eam_done
	move	a14,a2
	addi	easy_ali_y,a2
	move	*a2,a1
	or	a1,a0
#eam_done
	PULL	a1,a2
#eam_out	rets
