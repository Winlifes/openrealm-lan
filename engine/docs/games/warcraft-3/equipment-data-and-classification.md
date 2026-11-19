# Equipment data and item classification

## Source contract

The supplied Warcraft III 3.0.1.24371 resources distinguish item classification,
equipment type, equipment loadout slot, and item tag. They are separate domains.
`Scripts/common.j` declares Equipment as itemtype 7, Unknown as 8 and Any as 9.
Earlier declarations use Unknown 7 and Any 8. Permanent through Miscellaneous
remain 0 through 6. Unknown must never become the wildcard selector.

`Units/UnitMetaData.slk` maps `iequ` to `ItemData.equipment` and `itag` to
`ItemData.tag`. Both are authored string enums. `UI/UnitEditorData.txt`
provides the exact tokens and order:

| Domain | Values in numeric order |
| --- | --- |
| equipmentType | None, Head, Chest, Gloves, Boots, Ring, Primary, Offhand, Trinket |
| itemTag | Undefined, Droppable, Quest Reward, Boss Drop, Secret, Puzzle, World, Shop |

The corresponding Any constants are selectors, absent from these authored
tables. Older ItemData schemas omit both columns and have None/Undefined
semantics. Current stock rows confirm, for example, `ecrr` as Ring/Droppable,
`ernp` as Ring/Shop, and `eesq` as Trinket/Boss Drop. Classical items such as
`ratf` retain None/Undefined; an item name or its passive abilities do not
determine whether it is equipment.

The deployment evidence `r74-equipment-contract-source.json` records exact
resource hashes and the two original editor sections; `r74-equipment-metadata.json`
retains relevant metadata and nine original Forgotten Hollow starting items.
These are resource contracts. Finding native names in the supplied executable
does not prove its automatic-slot selection or replacement algorithm.

## Ownership and implementation

`g_metadata.c` loads the two columns through the ordinary ItemData schema.
`UnitsMetaData` includes both W3T modification IDs, so original-item overrides
and custom items inherit through the existing map override owner. Item entities
retain their concrete rawcode and bind the corresponding ItemData row again
after restoration; no item-instance copy of these enums is introduced.

`GetItemEquipmentType`/`BlzGetItemEquipmentType` and
`GetItemTag`/`BlzGetItemTag` return typed value handles from the current bound
row. Their token tables live in the game common header. Unrecognized authored
tokens produce an explicit native error instead of silently returning None.
Invalid item handles return typed null; exact retail invalid-handle behavior
has not been established and is not a parity claim.

`GetItemType` retains the stable first seven classes, reads the loaded root
VM's Equipment/Unknown constants for the variable part, and treats a class
without an Equipment constant as Unknown. Random selectors read that VM's
actual Any constant. They retain the existing two-pass candidate selection,
level and pickRandom checks. Missing required constants report a script error;
there is no guessed numeric default. The generic interpreter reader checks
constant status, exact enum type, payload size and handle validity, and does
not push or pop the caller's native arguments. It also works from a resumed
JASS or Lua coroutine through its root VM.

Native/browser save versions are 164/25, rejecting worlds whose script
variables or prior random choices can reflect the old classification meaning.
JASS snapshot 16 and network layouts remain unchanged. No new persistent
instance fields, saved C callbacks, entity-state fields or player-state fields
are added. This is a strict contract change, without migration.

## Verification and remaining equipment work

Seven production tests cover distinct ROC/TFT column schemas, legacy and
modern itemtype declarations, Any versus Unknown candidate pools, all four
metadata native bindings, base and custom W3T inheritance, invalid tokens,
and a whole-world save restored into a freshly created VM. Script assertions
are followed by an observed player-state counter. Two interpreter cases cover
typed constant rejection, unchanged output on failure, native arguments, root
lookups and resumed JASS/Lua execution. These verify data and classification;
they do not establish a complete equipment inventory.

The same original AbilityData distinguishes `AInv` (ordinary inventory),
`AInx` (expanded inventory), and `AEqu` (equipment inventory). The nine
`loadoutslot` constants include two separate ring slots. Original
`UnitEquipItemByIdSwapped` creates an item on the ground, calls
`BlzUnitEquipItem`, and only updates the last-equipped handle on success.
This prohibits implementing equip as ordinary six-slot insertion.

Still required: expanded-bag ownership and UI; nine-slot equip/unequip and
automatic slot selection; full-bag and occupied-slot behavior; ability/stat
application and removal; transfer, death, removal and save lifecycle; original
rendered input and gameplay acceptance. Automatic ring replacement, all
capacity transitions, and exact invalid-handle behavior need further original
code/runtime evidence. The two affected modern scenario maps remain
incomplete. Filtered random-item APIs with inclusion flags are also still
unimplemented; the existing two selectors above are the verified scope.
