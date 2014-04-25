This doesn't work yet.


xpc
===

```ruby
require 'xpc'

# ...

xpc = XPC.new(mach_service_name)

Signal.trap("INT") { xpc.disconnect }

xpc.on(:event) do |event|
  puts event
end

xpc.on(:error) do |error|
  puts error
end

xpc.connect

xpc.emit(message)


```
