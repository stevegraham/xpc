require 'mkmf'

$LDFLAGS += " -framework cocoa "

$defs.push "-DRUBY_19" if RUBY_VERSION > "1.8"

dir_config("xpc")
create_makefile("xpc")
