SRC_D = d1_mode.c d3_iota.c
SRC_E =
SRC_P = p1_sum.c p2_list.c p3_flood.c p4_write.c p5_hose.c p6_tee.c
SRC_R =
SRC_T =

CC       = /usr/bin/cc
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
	@rm -f *.core core *~ a.out .check.*.out valgrind.out a.out

.c: makefile .helper.sh
	@env $(_ENV) sh .helper.sh cc     $< -o $@ -MF ${@:%=.%.d}
	@env $(_ENV) sh .helper.sh grind ./$@

$(BIN_T): makefile .helper.sh
	@echo building and testing $@
	@env $(_ENV) sh .helper.sh cc     $@.c -o $@ -MF ${@:%=.%.d}
	@env $(_ENV) sh .helper.sh grind ./$@

-include $(DEP)

.PHONY: all clean
