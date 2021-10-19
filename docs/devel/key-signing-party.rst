Key-signing Party
=================

.. _whats_a_key_signing_party:

What's a key-signing party?
---------------------------

A key-signing party is a get-together with PGP users for the purpose of
meeting other PGP users and signing each other's keys. This helps to
extend the "web of trust" to a great degree. Also, it sometimes serves
as a forum to discuss strong cryptography and related issues. In QEMU we
use PGP keys to sign pull requests, so submaintainers need to have PGP
keys signed by those with direct commit access.

This wiki page gives general information on how we run key-signing
parties for QEMU; usually there will be one at KVM Forum. For details of
a specific event (location, organizer, etc) see the wiki page for that
event.

The instructions here are pretty specific, because there will likely be
at least a dozen people trying to arrange to sign each others' keys. To
get this done in a reasonable time we need to be efficient about it, so
following the instructions makes it easier and smoother for everyone. If
(for instance) you don't send your key to the organizer before the
deadline then it's quite likely you won't get your key signed.

.. _what_do_i_need_for_this_party:

What do I need for this party?
------------------------------

.. _required_items:

Required Items
~~~~~~~~~~~~~~

-  Physical attendance
-  Positive picture ID
-  Your Key ID, Key type, HEX fingerprint, and Key size
-  A pen/pencil or whatever you'd like to write with....
-  NO computer

.. _required_process:

Required Process
~~~~~~~~~~~~~~~~

-  Generate a key/Remember your pass phrase
-  All attendees send their public keys to a public keyserver. Unless
   specified otherwise, use keys.gnupg.net. If for some reason you don't
   want your key to be in a public keyserver, but still want to
   participate, please let me know.
-  All attendees send their key ID, key type, fingerprint, and key size
   to the host, who will compile everyone's key information.
-  The host prints a list with everyone's key ID, key type, fingerprint,
   and key size from the compiled keyrings and distributes copies of the
   printout at the meeting.
-  Attend the party. Bring along a paper copy of your key ID, key type,
   fingerprint, and key size that you obtained from your own keyring.
   You must also bring along a suitable photo ID. Instruct the attendees
   at the beginning that they are to make two marks on the listing, one
   for correct key information (key ID, key type, fingerprint, and key
   size) and one if the ID check is ok.
-  At the meeting each key owner reads his key ID, key type,
   fingerprint, key size, and user ID from his own printout, not from
   the distributed listing. This is because there could be an error,
   intended or not, on the listing. This is also the time to tell which
   ID's to sign or not. If the key information matches your printout
   then place a check-mark by the key.
-  After everyone has read his key ID information, have all attendees
   form a line.
-  The first person walks down the line having every person check his
   ID.
-  The second person follows immediately behind the first person and so
   on.
-  If you are satisfied that the person is who they say they are, and
   that the key on the printout is theirs, you place another check-mark
   next to their key on your printout.
-  Once the first person cycles back around to the front of the line he
   has checked all the other IDs and his ID has been checked by all
   others.
-  After everybody has identified himself or herself the formal part of
   the meeting is over. You are free to leave or to stay and discuss
   matters of PGP and privacy (or anything else) with fellow PGP users.
   If everyone is punctual the formal part of the evening should take
   less than an hour.
-  After confirming that the key information on the key server matches
   the printout that you have checked, sign the appropriate keys. Keys
   can only be signed if they have two check-marks. Note that it is
   really important to check the full fingerprint -- there are many keys
   on the keyservers are maliciously generated fakes which have the same
   short 32-bit keyid but the wrong fingerprint!
-  Send the signed keys back to the keyservers.
-  Use those keys as often as possible.

.. _why_shouldnt_i_bring_a_computer:

Why shouldn't I bring a computer?
---------------------------------

There are a variety of reasons, why you don't want to do this. The short
answer is it would be insecure, unsafe, and of no benefit. For those not
convinced, here are some reasons why it is insecure, unsafe, and of no
benefit.

-  Someone might have modified the computers programs, operating system,
   or hardware to steal or modify keys.
-  If people are swapping disks with their keys on them the computer
   owner has to worry about viruses.
-  If people are carrying their secret keys with them and intend to do
   the signing at the actual meeting by typing their passphrase into a
   computer, then they are open to key-logging attacks,
   shoulder-surfing, etc.
-  It is much better to just exchange key details and verify ID and then
   do the signing when you get home to your own trusted computer.
-  Someone might spill beer on it.
-  Someone might drop it or knock it off the table.
-  More reasons, I don't feel like articulating

.. _other_questions_about_signing_keys:

Other questions about signing keys?
-----------------------------------

You may want to read the `Keysigning Party
Howto <http://www.cryptnet.net/fdp/crypto/gpg-party.html>`__ which
includes an explanation of the concepts behind keysigning, instructions
for hosting a keysigning party, instructions for participating in a
keysigning party, and step by step instructions for signing other's
keys.

If you're looking for quick answers you may want to look to the
questions and answers below, which all come from the `PGP
FAQ <http://www.pgp.net/pgpnet/pgp-faq/faq.html>`__. It also has a lot
of other good information, besides what is linked to below.

-  `What is key
   signing? <http://www.pgp.net/pgpnet/pgp-faq/faq.html#KEY-SIGNING-WHAT>`__
-  `How do I sign a
   key? <http://www.pgp.net/pgpnet/pgp-faq/faq.html#KEY-SIGNING-HOW>`__
-  `Should I sign my own
   key? <http://www.pgp.net/pgpnet/pgp-faq/faq.html#KEY-SIGNING-SELF>`__
-  `Should I sign X's
   key? <http://www.pgp.net/pgpnet/pgp-faq/faq.html#KEY-SIGNING-WHEN>`__
-  `How do I verify someone's
   identity? <http://www.pgp.net/pgpnet/pgp-faq/faq.html#KEY-SIGNING-IDENTITY-CHECK>`__
-  `How do I know someone hasn't sent me a bogus key to
   sign? <http://www.pgp.net/pgpnet/pgp-faq/faq.html#KEY-SIGNING-KEY-VERIFICATION>`__

.. _other_useful_pgp_links:

Other useful PGP links
----------------------

A few more links for PGP newbies, or those who wish to re acquaint
themselves.

-  http://www.pgpi.org/ -- The International PGP Home Page
-  http://www.pgpi.org/download/ -- Download PGP
-  http://www.gnupg.org/ -- GNU PGP (Linux)
-  http://www.pgpi.org/products/tools/search/ -- PGP Tools, Shells, and
   Plugins

.. _what_if_i_still_have_a_question:

What if I still have a question?
--------------------------------

If you'd like some help answering it, you can contact the event
coordinator.
