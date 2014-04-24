Gem::Specification.new do |s|
  s.name        = 'xpc'
  s.version     = '0.0.1'
  s.authors     = ['Stevie Graham']
  s.email       = ['sjtgraham@mac.com']
  s.homepage    = 'https://github.com/stevegraham/xpc'
  s.summary     = 'Mac OS X XPC'
  s.description = 'Call into mach services via xpc'

  s.extensions << 'ext/extconf.rb'

  s.files = Dir["ext/*", "MIT-LICENSE", "README.MD"]
end
