**************************************************************
*
* Doink's pie throw (mods/doinkpie/gen.txt)
*
**************************************************************

	.file	"doinkpie.asm"
	.title	"doink pie"
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
	.include	"special.equ"
	.include	"imgtbl.glo"

	.ref	change_anim1a,doink_pie

	;Non-zero: Down, Down + kick throws the pie. The machine sets it while the
	;mod is switched on.
	BSSX	pie_enabled,16

	.text
	.even

******************************************************************************
* The secret move: same layout as the others in DOINK.ASM (newest first, then
* the joystick states before it, then the longest time allowed and the code).

 SUBR	dnk_pie_rec
	.word	B_KICK,			J_ALL
	.word	J_DOWN,			J_REAL_LR
	.word	J_DOWN,			J_REAL_LR
	.word	8000h | 32		;max
	.long	dnk_pie_secret

 SUBR	dnk_pie_secret
	move	@pie_enabled,a0
	jrz	#out

	move	*a13(ANIMODE),a0
	btst	MODE_UNINT_BIT,a0
	jrnz	#out

	move	*a13(PLYRMODE),a0
	cmpi	MODE_NORMAL,a0
	jrne	#out

	FACE24	dnk,pie_anim
	calla	change_anim1a
#out	rets

******************************************************************************
* The throw: his punch without the attack, the pie leaves the hand on the
* fourth frame (where the original has the commented out ANI_SNOT).

#PIE_SPD	equ	2

 SUBR	dnk_2_pie_anim

	.word	ANI_SETMODE,MODE_UNINT|MODE_NOAUTOFLIP
	.word	ANI_SETPLYRMODE,MODE_NORMAL
	.word	ANI_ZEROVELS
	.word	ANI_SETSPEED,100h
	.word	ANI_SETFACING
	.word	ANI_SET_WRESTLER_XFLIP

	WL	#PIE_SPD,D2PU3A+FR1
	WL	#PIE_SPD,D2PU3A+FR2
	WL	#PIE_SPD+1,D2PU3A+FR3
	WL	#PIE_SPD,D2PU3A+FR4
	WLWWWW	ANI_SNOT,doink_pie,0,0,0,0
	WL	#PIE_SPD+1,D2PU3A+FR5
	WL	#PIE_SPD,D2PU3A+FR6
	WL	#PIE_SPD+1,D2PU3A+FR7
	WL	#PIE_SPD,D2PU3A+FR8

	.word	ANI_SETMODE,MODE_NORMAL
	.word	ANI_END

 SUBR	dnk_4_pie_anim

	.word	ANI_SETMODE,MODE_UNINT|MODE_NOAUTOFLIP
	.word	ANI_SETPLYRMODE,MODE_NORMAL
	.word	ANI_ZEROVELS
	.word	ANI_SETSPEED,100h
	.word	ANI_SETFACING
	.word	ANI_SET_WRESTLER_XFLIP

	WL	#PIE_SPD,D4PU3C+FR1
	WL	#PIE_SPD,D4PU3C+FR2
	WL	#PIE_SPD+1,D4PU3C+FR3
	WL	#PIE_SPD,D4PU3C+FR4
	WLWWWW	ANI_SNOT,doink_pie,0,0,0,0
	WL	#PIE_SPD+1,D4PU3C+FR5
	WL	#PIE_SPD,D4PU3C+FR6
	WL	#PIE_SPD+1,D4PU3C+FR7
	WL	#PIE_SPD,D4PU3C+FR8

	.word	ANI_SETMODE,MODE_NORMAL
	.word	ANI_END

******************************************************************************
* The pie in the air (special object animation, SPECIAL.ASM): four turning
* frames, then again.

 SUBR	dnk_pie_flight

	.word	ASP_COLLBOX,-10,20,-8,16,-10,20	;xoff,width,yoff,height,zoff,depth
#lp
	WL	3,PIE01
	WL	3,PIE02
	WL	3,PIE03
	WL	3,PIE04
	WL	ASP_GOTO,#lp
