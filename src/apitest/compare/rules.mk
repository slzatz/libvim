# Used after src/Makefile (make -f Makefile -f apitest/compare/rules.mk), for
# its compiler, flags and libraries. build.sh does that.

COMPARE = apitest/compare

$(COMPARE)/%.exe: $(COMPARE)/%.c libvim.a
	$(CC) -I. $(ALL_CFLAGS) -g $< -o $@ libvim.a $(ALL_LIBS)

compare-tools: $(COMPARE)/probe.exe $(COMPARE)/trace.exe $(COMPARE)/api_edges.exe
