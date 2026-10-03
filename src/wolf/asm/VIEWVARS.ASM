**************************************************************
*
* Variables the machine (src/wolf/wolf.c) sets for the wide view. Not a mod:
* with the original 400x254 view (--classic) they are zero and the game
* behaves as before (src/wolf/core.gen.txt).
*
**************************************************************

	.file	"viewvars.asm"
	.title	"wide view variables"
	.width	132
	.option	b,d,l,t
	.mnolist

	.include	"macros.h"

	;Pixels the wide view shows beyond the original screen, each side.
	;keep_onscreen (WRESTLE2.ASM) lets a wrestler outside the ring go this
	;far beyond its usual limit.
	BSSX	view_extra,16

	;Non-zero: a wrestler may climb out of the ring as if an opponent were
	;already outside (any_opp_outside, WRESTLE2.ASM). wolf.c sets it with the
	;option "no ring-out timer", where nobody has to be out there to go out.
	BSSX	leave_ring,16

	;Non-zero: the character select clock (SELECT.ASM select_clock) does not run out, so nobody
	;is sent on because the time is up. wolf.c sets it with the option "no select timer".
	BSSX	no_select_timer,16

	;Non-zero: the round's clock (WRESTLE2.ASM #dec_timer) does not count down, so a round never ends on time.
	;wolf.c sets it with the option "no match timer".
	BSSX	no_match_timer,16

	;Non-zero: flash_white / flash_red (WRESTLE2.ASM), the full screen flashes of big hits and of
	;the finish, do nothing. wolf.c sets them with the options "no white flashes" / "no red flashes".
	BSSX	no_flash_white,16
	BSSX	no_flash_red,16

	;ck_climb_out_side reads a field through a0 after any_opp_outside, which
	;left a0 in the process pointer list; the early return points it at zeros.
	BSSX	leave_zero,800h


	.text
	.even
