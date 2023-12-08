SRC_D = d1_wait.c d2_rle.c
SRC_E =
SRC_P = p1_counter.c p2_mcount.c p3_meter.c p4_pipe.c p5_mpipe.c p6_compute.c
SRC_R =
SRC_T =

CC       = /packages/run.64/gcc-13.1/bin/gcc
VALGRIND = valgrind
CFLAGS   = -std=c99 -Wall -Wextra -Werror -MD -MP

-include ../local.mk

BIN_T    = ${SRC_T:.c=}
BIN_D    = ${SRC_D:.c=}
BIN_E    = ${SRC_E:.c=}
BIN_P    = ${SRC_P:.c=}
BIN_R    = ${SRC_R:.c=}

BIN      = $(BIN_T) $(BIN_D) $(BIN_E) $(BIN_P) $(BIN_R)
SRC      = $(SRC_T) $(SRC_D) $(SRC_E) $(SRC_P) $(SRC_R)
DEP      = ${BIN_D:%=.%.d} ${BIN_E:%=.%.d} ${BIN_T:%=.%.d} ${BIN_E:%=.sol.%.d}

_ENV   = CC="$(CC)" CFLAGS="$(CFLAGS)" VALGRIND="$(VALGRIND)"

all: $(BIN)

clean:
	@rm -f $(BIN) $(DEP)
	@rm -f *.core core *~ a.out .check.*.out .valgrind.*.out.* a.out

.c: makefile .helper.sh
	@env $(_ENV) sh .helper.sh cc     $< -o $@ -MF ${@:%=.%.d}
	@env $(_ENV) sh .helper.sh grind ./$@

$(BIN_T): makefile .helper.sh
	@echo building and testing $@
	@env $(_ENV) sh .helper.sh cc     $@.c -o $@ -MF ${@:%=.%.d}
	@env $(_ENV) sh .helper.sh grind ./$@

-include $(DEP)

.PHONY: all clean
