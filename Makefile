CC = gcc
PKGS = libadwaita-1 gtksourceview-5 webkitgtk-6.0 poppler-glib cairo
CFLAGS = -std=c17 -Wall -Wextra -O2 -MMD -MP $(shell pkg-config --cflags $(PKGS))
LDFLAGS = $(shell pkg-config --libs $(PKGS))

BUILDDIR = build
SRC = src/main.c src/window.c src/settings.c src/actions.c src/ssh.c src/preview.c
OBJ = $(patsubst src/%.c,$(BUILDDIR)/%.o,$(SRC))
DEP = $(OBJ:.o=.d)
BIN = $(BUILDDIR)/notes-md

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(BUILDDIR)/%.o: src/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

-include $(DEP)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

.PHONY: all clean
