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
	rm -f erya_test
.PHONY: erya clean
