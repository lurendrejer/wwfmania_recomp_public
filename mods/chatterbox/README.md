# chatterbox

The commentators comment on everything. `--mod chatterbox`.

Most comments come from a table through `ADD_TO_QUEUE` / `ADD_IF_SILENT` (`DCSSOUND.ASM`). Each call has a chance in
tenths of a percent (200 for a punch to the face, one in five), tested by `RNDPER`. `gen.txt` sets the chance to 1000
(always) while `chatter_on` is set.

`ADD_IF_SILENT` still says nothing while a line is playing, so the commentators do not talk over each other. They only
fill every silence.

Tried (headless, one CPU match): 28 commentator lines instead of 22. The count is limited by the lines' own length.
Unverified: the pin shouts (`PIN_HIM_PROC`) and other calls that roll their own chance are not changed.
