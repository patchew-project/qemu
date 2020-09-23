
NUMA mechanics for sPAPR (pseries machines)
============================================

NUMA in sPAPR works different than the System Locality Distance
Information Table (SLIT) in ACPI. The logic is explained in the LOPAPR
1.1 chapter 15, "Non Uniform Memory Access (NUMA) Option". This
document aims to complement this specification, providing details
of the elements that impacts how QEMU views NUMA in pseries.

Associativity and ibm,associativity property
--------------------------------------------

Associativity is defined as a group of platform resources that has
similar mean performance (or in our context here, distance) relative to
everyone else outside of the group.

The format of the ibm,associativity property varies with the value of
bit 0 of byte 5 of the ibm,architecture-vec-5 property. The format with
bit 0 equal to zero is deprecated. The current format, with the bit 0
with the value of one, makes ibm,associativity property represent the
physical hierarchy of the platform, as one or more lists that starts
with the highest level grouping up to the smallest. Considering the
following topology:

::

    Mem M1 ---- Proc P1    |
    -----------------      | Socket S1  ---|
          chip C1          |               |
                                           | HW module 1 (MOD1)
    Mem M2 ---- Proc P2    |               |
    -----------------      | Socket S2  ---|
          chip C2          |

The ibm,associativity property for the processors would be:

* P1: {MOD1, S1, C1, P1}
* P2: {MOD1, S2, C2, P2}

Each allocable resource has an ibm,associativity property. The LOPAPR
specification allows multiple lists to be present in this property,
considering that the same resource can have multiple connections to the
platform.

Relative Performance Distance and ibm,associativity-reference-points
--------------------------------------------------------------------

The ibm,associativity-reference-points property is an array that is used
to define the relevant performance/distance  related boundaries, defining
the NUMA levels for the platform.

The definition of its elements also varies with the value of bit 0 of byte 5
of the ibm,architecture-vec-5 property. The format with bit 0 equal to zero
is also deprecated. With the current format, each integer of the
ibm,associativity-reference-points represents an 1 based ordinal index (i.e.
the first element is 1) of the ibm,associativity array. The first
boundary is the most significant to application performance, followed by
less significant boundaries. Allocated resources that belongs to the
same performance boundaries are expected to have relative NUMA distance
that matches the relevancy of the boundary itself. Resources that belongs
to the same first boundary will have the shortest distance from each
other. Subsequent boundaries represents greater distances and degraded
performance.

Using the previous example, the following setting reference points defines
three NUMA levels:

* ibm,associativity-reference-points = {0x3, 0x2, 0x1}

The first NUMA level (0x3) is interpreted as the third element of each
ibm,associativity array, the second level is the second element and
the third level is the first element. Let's also consider that elements
belonging to the first NUMA level have distance equal to 10 from each
other, and each NUMA level doubles the distance from the previous. This
means that the second would be 20 and the third level 40. For the P1 and
P2 processors, we would have the following NUMA levels:

::

  * ibm,associativity-reference-points = {0x3, 0x2, 0x1}

  * P1: associativity{MOD1, S1, C1, P1}

  First NUMA level (0x3) => associativity[2] = C1
  Second NUMA level (0x2) => associativity[1] = S1
  Third NUMA level (0x1) => associativity[0] = MOD1

  * P2: associativity{MOD1, S2, C2, P2}

  First NUMA level (0x3) => associativity[2] = C2
  Second NUMA level (0x2) => associativity[1] = S2
  Third NUMA level (0x1) => associativity[0] = MOD1

  P1 and P2 have the same third NUMA level, MOD1: Distance between them = 40

Changing the ibm,associativity-reference-points array changes the performance
distance attributes for the same associativity arrays, as the following
example illustrates:

::

  * ibm,associativity-reference-points = {0x2}

  * P1: associativity{MOD1, S1, C1, P1}

  First NUMA level (0x2) => associativity[1] = S1

  * P2: associativity{MOD1, S2, C2, P2}

  First NUMA level (0x2) => associativity[1] = S2

  P1 and P2 does not have a common performance boundary. Since this is a one level
  NUMA configuration, distance between them is one boundary above the first
  level, 20.


In a hypothetical platform where all resources inside the same hardware module
is considered to be on the same performance boundary:

::

  * ibm,associativity-reference-points = {0x1}

  * P1: associativity{MOD1, S1, C1, P1}

  First NUMA level (0x1) => associativity[0] = MOD0

  * P2: associativity{MOD1, S2, C2, P2}

  First NUMA level (0x1) => associativity[0] = MOD0

  P1 and P2 belongs to the same first order boundary. The distance between then
  is 10.


How the pseries Linux guest calculates NUMA distances
=====================================================

Another key difference between ACPI SLIT and the LOPAPR regarding NUMA is
how the distances are expressed. The SLIT table provides the NUMA distance
value between the relevant resources. LOPAPR does not provide a standard
way to calculate it. We have the ibm,associativity for each resource, which
provides a common-performance hierarchy,  and the ibm,associativity-reference-points
array that tells which level of associativity is considered to be relevant
or not.

The result is that each OS is free to implement and to interpret the distance
as it sees fit. For the pseries Linux guest, each level of NUMA duplicates
the distance of the previous level, and the maximum amount of levels is
limited to MAX_DISTANCE_REF_POINTS = 4 (from arch/powerpc/mm/numa.c in the
kernel tree). This results in the following distances:

* both resources in the first NUMA level: 10
* resources one NUMA level apart: 20
* resources two NUMA levels apart: 40
* resources three NUMA levels apart: 80
* resources four NUMA levels apart: 160


Consequences for QEMU NUMA tuning
---------------------------------

The way the pseries Linux guest calculates NUMA distances has a direct effect
on what QEMU users can expect when doing NUMA tuning. As of QEMU 5.1, this is
the default ibm,associativity-reference-points being used in the pseries
machine:

ibm,associativity-reference-points = {0x4, 0x4, 0x2}

The first and second level are equal, 0x4, and a third one was added in
commit a6030d7e0b35 exclusively for NVLink GPUs support. This means that
regardless of how the ibm,associativity properties are being created in
the device tree, the pseries Linux guest will only recognize three scenarios
as far as NUMA distance goes:

* if the resources belongs to the same first NUMA level = 10
* second level is skipped since it's equal to the first
* all resources that aren't a NVLink GPU, it is guaranteed that they will belong
  to the same third NUMA level, having distance = 40
* for NVLink GPUs, distance = 80 from everything else

In short, we can summarize the NUMA distances seem in pseries Linux guests, using
QEMU up to 5.1, as follows:

* local distance, i.e. the distance of the resource to its own NUMA node: 10
* if it's a NVLink GPU device, distance: 80
* every other resource, distance: 40

This also means that user input in QEMU command line does not change the
NUMA distancing inside the guest for the pseries machine.

New NUMA mechanics for pseries in QEMU 5.2
==========================================

Starting in QEMU 5.2, the pseries machine now considers user input when
setting NUMA topology of the guest. The following changes were made:

* ibm,associativity-reference-points was changed to {0x4, 0x3, 0x2, 0x1}, allowing
  for 4 distinct NUMA distance values based on the NUMA levels

* ibm,max-associativity-domains was changed to support multiple associativity
  domains in all NUMA levels. This is needed to ensure user flexibility

* ibm,associativity for all resources now varies with user input

These changes are only effective for pseries-5.2 and newer machines that are
created with more than one NUMA node (disconsidering NUMA nodes created by
the machine itself, e.g. NVLink 2 GPUs). The now legacy support has been
around for such a long time, with users seeing NUMA distances 10 and 40
(and 80 if using NVLink2 GPUs), and there is no need to disrupt the
existing experience of those guests.

To bring the user experience x86 users have when tuning up NUMA, we had
to operate under the current pseries Linux kernel logic described in
`How the pseries Linux guest calculates NUMA distances`_. The result
is that we needed to translate NUMA distance user input to pseries
Linux kernel input.

Translating user distance to kernel distance
--------------------------------------------

User input for NUMA distance can vary from 10 to 254. We need to translate
that to the values that the Linux kernel operates on (10, 20, 40, 80, 160).
This is how it is being done:

* user distance 11 to 30 will be interpreted as 20
* user distance 31 to 60 will be interpreted as 40
* user distance 61 to 120 will be interpreted as 80
* user distance 121 and beyond will be interpreted as 160
* user distance 10 stays 10

The reasoning behind this aproximation is to avoid any round up to the local
distance (10), keeping it exclusive to the 4th NUMA level (which is still
exclusive to the node_id). All other ranges were chosen under the developer
discretion of what would be (somewhat) sensible considering the user input.
Any other strategy can be used here, but in the end the reality is that we'll
have to accept that a large array of values will be translated to the same
NUMA topology in the guest, e.g. this user input:

::

      0   1   2
  0  10  31 120
  1  31  10  30
  2 120  30  10

And this other user input:

::

      0   1   2
  0  10  60  61
  1  60  10  11
  2  61  11  10

Will both be translated to the same values internally:

::

      0   1   2
  0  10  40  80
  1  40  10  20
  2  80  20  10

Users are encouraged to use only the kernel values in the NUMA definition to
avoid being taken by surprise with that the guest is actually seeing in the
topology. There are enough potential surprises that are inherent to the
associativity domain assignment process, discussed below.


How associativity domains are assigned
--------------------------------------

LOPAPR allows more than one associativity array (or 'string') per allocated
resource. This would be used to represent that the resource has multiple
connections with the board, and then the operational system, when deciding
NUMA distancing, should consider the associativity information that provides
the shortest distance.

The spapr implementation does not support multiple associativity arrays per
resource, neither does the pseries Linux kernel. We'll have to represent the
NUMA topology using one associativity per resource, which means that choices
and compromises are going to be made.

Consider the following NUMA topology entered by user input:

::

      0   1   2   3
  0  10  20  20  40
  1  20  10  80  40
  2  20  80  10  20
  3  40  40  20  10

Honoring just the relative distances of node 0 to every other node, one possible
value for all associativity arrays would be:

* node 0: 0 B A 0
* node 1: 0 0 A 1
* node 2: 0 0 A 2
* node 3: 0 B 0 3

With the reference points {0x4, 0x3, 0x2, 0x1}, for node 0:

* distance from 0 to 1 is 20 (no match at 0x4, will match at 0x3)
* distance from 0 to 2 is 20 (no match at 0x4, will match at 0x3)
* distance from 0 to 3 is 40 (no match at 0x4 and 0x3, will match
  at 0x2)

The distances related to node 0 are well represented. Doing for node 1, and keeping
in mind that we don't need to revisit node 0 again, the distance from node 1 to
2 is 80, matching at 0x4:

* node 1: C 0 A 1
* node 2: C 0 A 2

Over here we already have the first conflict. Even if we assign a new associativity
domain at 0x4 for 1 and 2, and we do that in the code, the kernel will define
the distance between 1 and 2 as 20, not 80, because both 1 and 2 have the "A"
associativity domain from the previous step. If we decide to discard the
associativity with "A" then the node 0 distances are compromised.

Following up with the distance from 1 to 3 being 40 (a match in 0x2) we have another
decision to make. These are the current associativity domains of each:

* node 1: C 0 A 1
* node 3: 0 B 0 3

There is already an associativity domain at 0x2 in node 3, "B", which was assigned
by the node 0 distances. If we define a new associativity domain at this level
for 1 and 3 we will overwrite the existing associativity between 0 and 3. What
the code is doing in this case is to assign the existing domain to the
current associativity, in this case, "B" is now assigned to the 0x2 of node 1,
resulting in the following associativity arrays:

* node 0: 0 B A 0
* node 1: C 0 A 1
* node 2: C B A 2
* node 3: 0 B 0 3

In the last step we will analyze just nodes 2 and 3. The desired distance between
2 and 3 is 20, i.e. a match in 0x3. Node 2 already has a domain assigned in 0x3,
A, so we do the same as we did in the previous case and assign it to node 3
at 0x3. This is the end result for the associativity arrays:

* node 0: 0 B A 0
* node 1: C 0 A 1
* node 2: C B A 2
* node 3: 0 B A 3

The kernel will read these arrays and will calculate the following NUMA topology for
the guest:

::

      0   1   2   3
  0  10  20  20  20
  1  20  10  20  20
  2  20  20  10  20
  3  20  20  20  10

Which is not what the user wanted, but it is what the current logic and implementation
constraints of the kernel and QEMU will provide inside the LOPAPR specification.

Changing a single value, specially a low distance value, makes for drastic changes
in the result. For example, with the same user input from above, but changing the
node distance from 0 to 1 to 40:

::

      0   1   2   3
  0  10  40  20  40
  1  40  10  80  40
  2  20  80  10  20
  3  40  40  20  10

This is the result inside the guest, applying the same heuristics:

::

  $ numactl -H
  available: 4 nodes (0-3)
  (...)
  node distances:
  node   0   1   2   3
    0:  10  40  20  20
    1:  40  10  80  40
    2:  20  80  10  20
    3:  20  40  20  10

This result is much closer to the user input and only a single distance was changed
from the original.

The kernel will always match with the shortest associativity domain possible, and we're
attempting to retain the previous established relations between the nodes. This means
that a distance equal to 20 between nodes A and B and the same distance 20 between nodes
A and F will cause the distance between B and F to also be 20. The same will happen to
other distances, but shorter distances has precedent over it to the distance calculation.

Users are welcome to use this knowledge and experiment with the input to get the
NUMA topology they want, or as closer as they want. The important thing is to keep
expectations up to par with what we are capable of provide at this moment: an
approximation.
