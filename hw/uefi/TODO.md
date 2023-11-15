
uefi variable service - todo list
---------------------------------

* implement reading/writing variable update time.
* implement authenticated variable updates.
  - used for 'dbx' updates.

known issues and limitations
----------------------------

* secure boot variables are read-only
  - due to auth vars not being implemented yet.
* works only on little endian hosts
  - accessing structs in guest ram is done without endian conversion.
* works only for 64-bit guests
  - UINTN is mapped to uint64_t, for 32-bit guests that would be uint32_t
