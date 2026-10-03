**************************************************************
*
* Bam Bam's bowling throw (mods/bamfire/gen.txt)
*
**************************************************************

	.file	"bamfire.asm"
	.title	"bam bam fireball mod"
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
	.include	"imgtbl.glo"

	.ref	change_anim1a,bam_fireball

	;Non-zero: Down, Toward, Punch throws the fireball. The machine sets
	;it while the mod is switched on.
	BSSX	bowl_enabled,16

	.text
	.even

******************************************************************************
* The secret move: same layout as the others in BAM.ASM (newest first, then
* the joystick states before it, then the longest time allowed and the code).

 SUBR	bam_bowl_rec
	.word	B_PUNCH,		J_ALL
	.word	J_TOWARD,		J_REAL_LR | J_UP | J_DOWN
	.word	J_DOWN,			J_REAL_LR
	.word	8000h | 40		;max
	.long	bam_bowl_secret

 SUBR	bam_bowl_secret
	move	@bowl_enabled,a0
	jrz	#out

	move	*a13(ANIMODE),a0
	btst	MODE_UNINT_BIT,a0
	jrnz	#out

	move	*a13(PLYRMODE),a0
	cmpi	MODE_NORMAL,a0
	jrne	#out

	FACE24	bam,bowl_anim
	calla	change_anim1a
#out	rets

******************************************************************************
* The image pointers the animation script points at.

bowl_tbl
	.long	BAMBOWL01,BAMBOWL02,BAMBOWL03,BAMBOWL04,BAMBOWL05
	.long	BAMBOWL06,BAMBOWL07,BAMBOWL08,BAMBOWL09,BAMBOWL10
	.long	BAMBOWL11,BAMBOWL12,BAMBOWL13

#BOWL_SPD	equ	3

 SUBR	bam_2_bowl_anim
 SUBR	bam_4_bowl_anim

	.word	ANI_SETMODE,MODE_UNINT|MODE_NOAUTOFLIP
	.word	ANI_SETPLYRMODE,MODE_NORMAL
	.word	ANI_ZEROVELS
	.word	ANI_SETSPEED,100h
	.word	ANI_SETFACING
	.word	ANI_SET_WRESTLER_XFLIP

	WL	#BOWL_SPD,bowl_tbl
	WL	#BOWL_SPD,bowl_tbl+FR2-FR1
	WL	#BOWL_SPD,bowl_tbl+FR3-FR1
	WL	#BOWL_SPD,bowl_tbl+FR4-FR1
	WL	#BOWL_SPD,bowl_tbl+FR5-FR1
	WL	#BOWL_SPD,bowl_tbl+FR6-FR1
	WL	#BOWL_SPD,bowl_tbl+FR7-FR1
	WL	#BOWL_SPD,bowl_tbl+FR8-FR1
	;let go
	WLWWWW	ANI_SNOT,bam_fireball,0,0,0,0
	WL	#BOWL_SPD,bowl_tbl+FR9-FR1
	WL	#BOWL_SPD,bowl_tbl+FR10-FR1
	WL	#BOWL_SPD,bowl_tbl+FR11-FR1
	WL	#BOWL_SPD,bowl_tbl+FR12-FR1
	WL	#BOWL_SPD,bowl_tbl+FR13-FR1

	.word	ANI_SETMODE,MODE_NORMAL
	.word	ANI_END

******************************************************************************
* The image headers of BAMFIRE.LOD (written by tools/gsp/genimg.py).

	.even
	.include "bamfire.tbl"
