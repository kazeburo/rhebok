# coding: utf-8
lib = File.expand_path('../lib', __FILE__)
$LOAD_PATH.unshift(lib) unless $LOAD_PATH.include?(lib)
require 'rhebok/version'

Gem::Specification.new do |spec|
  spec.name          = "rhebok"
  spec.version       = Rhebok::VERSION
  spec.authors       = ["Masahiro Nagano"]
  spec.email         = ["kazeburo@gmail.com"]
  spec.summary       = %q{High Perfomance Perforked Rack Handler}
  spec.description   = %q{High Perfomance and Optimized Perforked Rack Handler}
  spec.homepage      = "https://github.com/kazeburo/rhebok"
  spec.license       = "MIT"

  spec.files         = `git ls-files -z`.split("\x0")
  spec.executables   = spec.files.grep(%r{^bin/}) { |f| File.basename(f) }
  spec.test_files    = spec.files.grep(%r{^(test|spec|features)/})
  spec.require_paths = ["lib"]

  spec.add_development_dependency "bundler", "~> 1.7"
  spec.add_development_dependency "rake", "~> 10.0"
  spec.add_development_dependency "bacon"
  spec.add_dependency "prefork_engine", "~> 0.0.2"

  # get an array of submodule dirs by executing 'pwd' inside each submodule
  `git submodule --quiet foreach pwd`.split($\).each do |submodule_path|
    # for each submodule, change working directory to that submodule
    Dir.chdir(submodule_path) do
 
      # issue git ls-files in submodule's directory
      submodule_files = `git ls-files`.split($\)
 
      # prepend the submodule path to create absolute file paths
      submodule_files_fullpaths = submodule_files.map do |filename|
        "#{submodule_path}/#{filename}"
      end
 
      # remove leading path parts to get paths relative to the gem's root dir
      # (this assumes, that the gemspec resides in the gem's root dir)
      submodule_files_paths = submodule_files_fullpaths.map do |filename|
        filename.gsub "#{File.dirname(__FILE__)}/", ""
      end
 
      # add relative paths to gem.files
      spec.files += submodule_files_paths
    end
  end

end
