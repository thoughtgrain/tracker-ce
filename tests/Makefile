# Runs every host test suite: each folder here that has a Makefile.
#   make            run them all
#   make clean
# FW_ROOT is passed down to suites that take it (see README.md).
SUITES := $(sort $(patsubst %/Makefile,%,$(wildcard */Makefile)))

test: $(SUITES)

$(SUITES):
	$(MAKE) -C $@ test

clean:
	@for suite in $(SUITES); do $(MAKE) -C $$suite clean; done

.PHONY: test clean $(SUITES)
