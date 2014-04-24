require 'xpc'

xpc = XPC.new('com.apple.blued')

Signal.trap("INT") { xpc.disconnect }

xpc.on(:event) do |event|
  puts event
end

xpc.on(:error) do |error|
  puts error
end

xpc.connect
