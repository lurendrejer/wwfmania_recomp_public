**************************************************************
*
* The Adam Bomb mod's own data and code (mods/adambomb/gen.txt)
*
**************************************************************

	.file	"adammod.asm"
	.title	"adam bomb mod"
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
	.include	"adamb.h"

	.ref	ADMUG_A,ADMUG_B,ADMUG_C,ADMUG_D,ADMUG_E,ADMUG_F,ADMUG_G,ADMUG_H

	.text
	.even

;Adam's mugshot (IMG/WWFMUGS.IMG, which the shipped game loads without it;
;SELECT.ASM has it commented out as ADAMMUG), for his select square (mod.c)
 SUBR	adam_mug
	.long	ADMUG_A,ADMUG_B,ADMUG_C,ADMUG_D
	.long	ADMUG_E,ADMUG_F,ADMUG_G,ADMUG_H,0


******************************************************************************
* Adam on the screens between matches (PROGRESS.ASM), as Razor's there

 SUBR	adm_pg_wait_anim
	WL	100,AB4LD4A+FR1
	.word	ANI_END

 SUBR	adm_pg_clever_anim
	WL	6,AB4LD4A+FR1
	WL	6,AB4LD4A+FR2
	WL	6,AB4LD4A+FR3
	WL	6,AB4LD4A+FR4
	WL	6,AB4LD4A+FR5
	WL	6,AB4LD4A+FR6
	WL	6,AB4LD4A+FR7
	WL	6,AB4LD4A+FR2
	.word	ANI_END

 SUBR	adm_pg_taunt_anim
	WL	4,AB4CO4A+FR1
	WL	4,AB4CO4A+FR2
	WL	4,AB4CO4A+FR3
	WL	4,AB4CO4A+FR4
#loop	WL	15,AB4CO4A+FR5
	WL	4,AB4CO4A+FR6
	WL	4,AB4CO4A+FR7
	WL	4,AB4CO4A+FR8
	WL	4,AB4CO4A+FR7
	WL	4,AB4CO4A+FR6
	WL	4,AB4CO4A+FR5
	WL	4,AB4CO4A+FR6
	WL	4,AB4CO4A+FR7
	WL	4,AB4CO4A+FR8
	WL	4,AB4CO4A+FR7
	WL	4,AB4CO4A+FR6
	WL	ANI_GOTO,#loop

	.end
