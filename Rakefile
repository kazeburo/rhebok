require "bundler/gem_tasks"
require "rake/extensiontask"

Rake::ExtensionTask.new "rhebok" do |ext|
  ext.lib_dir = "lib/rhebok"
end

task :bacon do
  opts     = ENV['TEST'] || '-a'
  specopts = ENV['TESTOPTS'] || '-q'
  sh "bacon -I./lib:./test -w #{opts} #{specopts}"
end

task :test do
  Rake::Task["clobber"].invoke
  Rake::Task["compile"].invoke
  Rake::Task["bacon"].invoke
end


