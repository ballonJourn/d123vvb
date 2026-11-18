# Host simulator: the board's own rtthread/ble_img_rx.c behind a pipe.
sim_device: sim_device.c ../../rtthread/ble_img_rx.c ../../rtthread/ble_img_rx.h ../../rtthread/bnp_proto.h
	$(CC) -O2 -Wall -I../../rtthread -o $@ sim_device.c ../../rtthread/ble_img_rx.c

test: sim_device
	node run_sim.js

e2e: sim_device
	python3 e2e_page.py

.PHONY: test e2e
