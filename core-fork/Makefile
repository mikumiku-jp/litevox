UPSTREAM_DIR ?= voicevox_core
PROFILE ?= talk-only
LIB_PATH ?= $(UPSTREAM_DIR)/target/release/libvoicevox_core.dylib

check:
	test -d $(UPSTREAM_DIR)
	printf 'upstream_dir\t%s\n' '$(UPSTREAM_DIR)'
	printf 'profile\t%s\n' '$(PROFILE)'
	printf 'lib_path\t%s\n' '$(LIB_PATH)'

profile:
	cat profile-talk-only.env

.PHONY: check profile
