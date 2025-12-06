CTRMML = ctrmml
CTRMML_SRC = $(CTRMML)/src
CTRMML_LIB = $(CTRMML)/lib
LIBCTRMML  = ctrmml

ifneq ($(RELEASE),1)
LIBCTRMML := $(LIBCTRMML)_debug
endif

# ctrmml library is in a local submodule and only a static library can be built
CFLAGS += -I$(CTRMML_SRC)
LDFLAGS += -L$(CTRMML_LIB) -l$(LIBCTRMML)

LIBCTRMML_CHECK := $(CTRMML_LIB)/lib$(LIBCTRMML).a

# Ensure ctrmml is on the rng-patterns branch before building
.PHONY: ctrmml-checkout
ctrmml-checkout:
	@if [ -d "$(CTRMML)/.git" ]; then \
		echo "Checking out rng-patterns branch in $(CTRMML)..."; \
		cd $(CTRMML) && git checkout rng-patterns; \
	fi

# Make library build depend on checkout
$(LIBCTRMML_CHECK): ctrmml-checkout