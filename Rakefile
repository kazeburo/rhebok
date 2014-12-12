require "bundler/gem_tasks"
require "rspec/core/rake_task"
require "rake/extensiontask"

Rake::ExtensionTask.new "rhebok" do |ext|
  ext.lib_dir = "lib/rhebok"
end

RSpec::Core::RakeTask.new(:spec) do |spec|
  spec.pattern = 'spec/*_spec.rb'
  # spec.rspec_opts = ['-cfs']
end

task :test do
  Rake::Task["clobber"].invoke
  Rake::Task["compile"].invoke
  Rake::Task["spec"].invoke
end


