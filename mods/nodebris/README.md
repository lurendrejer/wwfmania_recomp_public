# nodebris

No blood and no debris.

The game has this switch itself, `no_debris` (`WRESTLE.ASM`): the ring dust, the debris of the finishers and the
blood of the broken arm (`BROKEN_ARM_BLOOD`) all check it. The mod holds it at 1 every frame (some moves clear it
around their own effects and restore it). The projectiles and splats (pie, fire) are not debris and stay.

Unverified: which of the effects are stopped in every case; only the game's own checks of the variable are
relied on.
