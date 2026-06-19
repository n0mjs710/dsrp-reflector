# dsrp-reflector — dependency-free C11 build

CC      ?= cc
CSTD    ?= c11
CFLAGS  ?= -std=$(CSTD) -O2 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L
LDFLAGS ?=

BIN     := dsrp-reflector
SRCDIR  := src
SRCS    := $(wildcard $(SRCDIR)/*.c)
OBJS    := $(SRCS:.c=.o)

# Install locations (override on the command line, e.g. PREFIX=/usr).
# DESTDIR is honored for staged/packaged installs.
DESTDIR    ?=
PREFIX     ?= /usr/local
BINDIR     ?= $(PREFIX)/bin
SYSCONFDIR ?= /etc
UNITDIR    ?= /lib/systemd/system
SVCUSER    ?= dsrp

CONFIG := $(SYSCONFDIR)/dsrp-reflector.ini
UNIT   := $(UNITDIR)/dsrp-reflector.service

.PHONY: all clean install uninstall

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) $(OBJS) $(BIN)

# Run as root: sudo make install
install: $(BIN)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	# Create an unprivileged service account (no-op if it exists).
	@if [ -z "$(DESTDIR)" ] && ! id -u $(SVCUSER) >/dev/null 2>&1; then \
		echo "creating system user $(SVCUSER)"; \
		useradd --system --no-create-home --shell /usr/sbin/nologin $(SVCUSER) || true; \
	fi
	# Install the config, but never clobber an existing one.
	install -d $(DESTDIR)$(SYSCONFDIR)
	@if [ -e "$(DESTDIR)$(CONFIG)" ]; then \
		echo "keeping existing $(DESTDIR)$(CONFIG); writing sample alongside"; \
		install -m 0644 dsrp-reflector.ini $(DESTDIR)$(CONFIG).sample; \
	else \
		install -m 0644 dsrp-reflector.ini $(DESTDIR)$(CONFIG); \
	fi
	# Install the systemd unit.
	install -d $(DESTDIR)$(UNITDIR)
	install -m 0644 systemd/dsrp-reflector.service $(DESTDIR)$(UNIT)
	@if [ -z "$(DESTDIR)" ] && command -v systemctl >/dev/null 2>&1; then \
		systemctl daemon-reload; \
		echo "Installed. Enable and start with:  sudo systemctl enable --now dsrp-reflector"; \
	else \
		echo "Installed. Reload systemd and enable the service to start it."; \
	fi

uninstall:
	@if [ -z "$(DESTDIR)" ] && command -v systemctl >/dev/null 2>&1; then \
		systemctl disable --now dsrp-reflector 2>/dev/null || true; \
	fi
	$(RM) $(DESTDIR)$(BINDIR)/$(BIN)
	$(RM) $(DESTDIR)$(UNIT)
	@if [ -z "$(DESTDIR)" ] && command -v systemctl >/dev/null 2>&1; then \
		systemctl daemon-reload || true; \
	fi
	@echo "Left $(DESTDIR)$(CONFIG) in place (remove manually if desired)."
