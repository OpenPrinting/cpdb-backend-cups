EXTRA_DIST = \
	org.cups.cupsd.Notifier.xml \
	org.openprinting.Backend.CUPS \
	org.openprinting.Backend.CUPS.service.in

# Dbus service file
servicedir = $(DBUS_SERVICES_DIR)
service_in_files = org.openprinting.Backend.CUPS.service.in
service_DATA = $(service_in_files:.service.in=.service)

$(service_DATA): $(service_in_files) Makefile
	@sed -e "s|\@cpdb_backend_dir\@|$(CPDB_BACKEND_DIR)|" $<> $@

info_backenddir = $(INFO_DIR)
info_backend_DATA = org.openprinting.Backend.CUPS

clean-local:
	rm -rf $(service_DATA)
