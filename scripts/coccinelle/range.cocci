/*
  Usage:

    spatch \
           --macro-file scripts/cocci-macro-file.h \
           --sp-file scripts/coccinelle/range.cocci \
           --keep-comments \
           --in-place \
           --dir .

  Description:
    Find out the range overlap check and use ranges_overlap() instead.

  Note:
    This pattern cannot accurately match the region overlap check, and you
    need to manually delete the use cases that do not meet the conditions.

    In addition, the parameters of ranges_overlap() may be filled incorrectly,
    and some use cases may be better to use range_overlaps_range().
*/

@@
expression E1, E2, E3, E4;
@@
(
- E2 <= E3 || E1 >= E4
+ !ranges_overlap(E1, E2, E3, E4)
|

- (E2 <= E3) || (E1 >= E4)
+ !ranges_overlap(E1, E2, E3, E4)
|

- E1 < E4 && E2 > E3
+ ranges_overlap(E1, E2, E3, E4)
|

- (E1 < E4) && (E2 > E3)
+ ranges_overlap(E1, E2, E3, E4)
|

- (E1 >= E3 && E1 < E4) || (E2 > E3 && E2 <= E4)
+ ranges_overlap(E1, E2, E3, E4)

|
- ((E1 >= E3) && (E1 < E4)) || ((E2 > E3) && (E2 <= E4))
+ ranges_overlap(E1, E2, E3, E4)
)

