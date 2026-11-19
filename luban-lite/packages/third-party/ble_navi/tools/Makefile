# Host tests for ble_navi. Needs only gcc. cJSON comes from the SDK tree.
FW    := ..
# the cJSON the firmware links (helloworld app); SDK copy: make CJSON=../../cJSON-1.7.16
CJSON ?= ../../../../application/rt-thread/helloworld/third/json
CFLAGS := -O1 -g -Wall -Wextra -Werror -std=c99 -D_POSIX_C_SOURCE=199309L \
          -fsanitize=address,undefined -fno-sanitize-recover=all -I$(FW) -I$(CJSON) -Ishim

erya: erya_test
	./erya_test $(JPG)

erya_test: erya_test.c $(FW)/erya_proto.c $(FW)/erya_json.c $(FW)/erya_proto.h $(CJSON)/cJSON.c
	$(CC) $(CFLAGS) erya_test.c $(FW)/erya_proto.c $(FW)/erya_json.c $(CJSON)/cJSON.c -o $@

clean:
	rm -f erya_test erya_replay h4_fuzz
.PHONY: erya clean

# web/erya_sender.js -> firmware engine interop (needs node)
JPG ?= ../web/sample_nav.jpg
erya_replay: erya_replay.c $(FW)/erya_proto.c $(FW)/erya_json.c $(FW)/erya_proto.h
	$(CC) $(CFLAGS) erya_replay.c $(FW)/erya_proto.c $(FW)/erya_json.c $(CJSON)/cJSON.c -o $@
erya-web: erya_replay
	node erya_js_test.mjs $(JPG) /tmp/erya_web_writes.bin
	./erya_replay /tmp/erya_web_writes.bin $(JPG)
.PHONY: erya-web

# web/erya.html + erya_app.js clicked through in jsdom with a fake BLE device,
# then every recorded write replayed into the firmware engine.
# One-time: npm i --prefix . jsdom@24 @napi-rs/canvas
erya-ui: erya_replay
	node erya_web_ui_test.mjs /tmp/erya_ui_writes.bin /tmp/erya_ui_last.jpg
	./erya_replay /tmp/erya_ui_writes.bin /tmp/erya_ui_last.jpg
.PHONY: erya-ui

# patched nimble hci_h4.c under random UART byte loss (ASan/UBSan): no assert,
# no leak, full resync on a clean stream
NIMBLE := ../../nimble
h4_fuzz: h4_fuzz.c $(NIMBLE)/nimble/transport/common/hci_h4/src/hci_h4.c
	$(CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Ih4_shim \
	  -I$(NIMBLE)/nimble/transport/common/hci_h4/include h4_fuzz.c \
	  $(NIMBLE)/nimble/transport/common/hci_h4/src/hci_h4.c -o $@
h4: h4_fuzz
	./h4_fuzz
.PHONY: h4
