qapi-gen-type = $(subst .,-,$(suffix $@))

qapi-modules = $(SRC_PATH)/qapi-schema.json $(SRC_PATH)/qapi/common.json \
       $(SRC_PATH)/qapi/block.json $(SRC_PATH)/qapi/block-core.json \
       $(SRC_PATH)/qapi/event.json $(SRC_PATH)/qapi/introspect.json \
       $(SRC_PATH)/qapi/crypto.json $(SRC_PATH)/qapi/rocker.json \
       $(SRC_PATH)/qapi/trace.json

qapi-py = $(SRC_PATH)/scripts/qapi.py $(SRC_PATH)/scripts/ordereddict.py
qapi-types-py = $(SRC_PATH)/scripts/qapi-types.py $(qapi-py)
qapi-visit-py = $(SRC_PATH)/scripts/qapi-visit.py $(qapi-py)
qapi-commands-py = $(SRC_PATH)/scripts/qapi-commands.py $(qapi-py)
qapi-introspect-py = $(SRC_PATH)/scripts/qapi-introspect.py $(qapi-py)

