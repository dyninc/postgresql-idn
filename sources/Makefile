OBJS = idn.o
MODULE_big = idn
EXTENSION = idn
DATA = idn--0.2.sql
DOCS =
REGRESS = idn

SHLIB_LINK = $(shell pkg-config libidn --libs) -lidn2
PG_CPPFLAGS = $(shell pkg-config libidn --cflags)

# can't use -Werror and -Wredundant-decls at the same time
PG_CPPFLAGS =
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
