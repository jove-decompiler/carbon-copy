.PHONY: all
all: collect extract

.PHONY: extract
extract:
	$(MAKE) -C $@

.PHONY: collect
collect:
	$(MAKE) -C $@

.PHONY: clean
clean:
	$(MAKE) -C extract clean
	$(MAKE) -C collect clean

.PHONY: install
install:
	$(MAKE) -C extract install
	$(MAKE) -C collect install
