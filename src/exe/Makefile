# favia: executor

%.o: $(FAVIA_DIR)/src/exe/%.c
	$(C) $(CFLAGS) $< -o $@

EXE_LINKFLAGS := $(LINKFLAGS)
ifeq "$(OS)" "linux"
else
	EXE_LINKFLAGS += -lws2_32 -lole32 -static-libgcc
endif

EXE := favia$(DOTEXE)
$(EXE): main.o libfavia.$(SO)
	$(LINK) $+ $(EXE_LINKFLAGS) $(LINK_RPATH_ORIGIN) -Wl,-rpath-link $(VLIB3_DIR) -o $@
