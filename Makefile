SHELL=/bin/bash
CPP=g++
CPPFLAGS=-O3 -Wall -std=c++11 -Werror -MMD -MP -mtune=native -ffast-math -funsafe-math-optimizations 
LDFLAGS=-lboost_unit_test_framework
BUILDDIR=$(CURDIR)/build

TESTS=$(foreach f,MessageQueueTest ShmemMessageQueueTest,tests/$(f))
all: $(TESTS)

$(BUILDDIR)/%.o: src/%.cpp
	@mkdir -p $(BUILDDIR)
	$(CPP) $(CPPFLAGS) "$<" -c -o "$@"

-include $(wildcard $(CURDIR)/build/*.d)

define build-test

$(1): $$(BUILDDIR)/$(notdir $(1)).o
	@mkdir -p $$(dir $(1))
	$$(CPP) $$(LDFLAGS) $$^ -o "$$@"

endef

tests/LoggingTest: $(CURDIR)/build/Logging.o

.PHONY: clean

clean:
	rm -f $(BUILDDIR)/*.{o,d} $(TESTS)

$(foreach b,$(TESTS),$(eval $(call build-test,$b)))

