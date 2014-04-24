xpc
===

```ruby
require 'xpc'

# ...

xpc = XPC.new(mach_service_name)

xpc.on(:event) do |event|
  # Do your thing
end

xpc.on(:error) do |error|
  # Whoops
end

xpc.connect

xpc.emit(message)

Signal.trap("INT") { xpc.disconnect }

```
