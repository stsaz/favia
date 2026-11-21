# favia: executor

%.o: $(FAVIA_DIR)/src/exe/%.c
	$(C) $(CFLAGS) $< -o $@

EXE_LINKFLAGS := $(LINKFLAGS) \
	-L$(VLIB3_DIR) -lffmpeg -lSDL3
ifeq "$(OS)" "linux"
else
	EXE_LINKFLAGS += -lws2_32 -lole32 -static-libgcc -static-libstdc++
endif

EXE := favia$(DOTEXE)
$(EXE): $(OBJS) exe.o
	$(LINK) $+ $(EXE_LINKFLAGS) $(LINK_RPATH_ORIGIN) -lm -o $@
